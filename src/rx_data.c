#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <alloca.h>
#include <assert.h>

#include "compat.h"
#include "wfb_params.h"

#include "frame_wfb.h"
#include "crypto_wfb.h"
#include "rx_core.h"
#include "rx_data.h"
#include "util_rbuf.h"
#include "util_msg.h"

static inline uint64_t
blk_get_seq(struct rx_context *ctx, struct rbuf_block *blk)
{
	uint64_t seq;

	seq = blk->index * ctx->fec_k + blk->fragment_to_send;

	return seq;
}

static void
send_data_one(struct rx_context *ctx, struct rbuf_block *blk)
{
	struct wfb_data_hdr *hdr;
	uint16_t pktlen;
	bool is_fec;
	uint64_t seq;

	hdr = (struct wfb_data_hdr *)blk->fragment[blk->fragment_to_send];
	pktlen = be16toh(hdr->packet_size);
	is_fec = (blk->fragment_len[blk->fragment_to_send] == 0);
	seq = blk_get_seq(ctx, blk);

	if (ctx->rx_ring->last_seq > 0) {
		if (seq > ctx->rx_ring->last_seq + 1) {
			p_debug("Missing Frame: %llu\n",
			   seq - ctx->rx_ring->last_seq - 1);
		}
	}
	ctx->rx_ring->last_seq = seq;

	if (hdr->flags & WFB_PACKET_F_FEC_ONLY)
		return; // Null frame to complete FEC
	if (pktlen > MAX_PAYLOAD_SIZE) {
		p_info("Malformed frame detected. Invalid FEC?\n");
		return;
	}


	if (ctx->n_decode_handler > 0)
		rx_decode_frame(ctx, (uint8_t *)(hdr + 1), pktlen);
	else
		p_debug("Received %llu: %d bytes, BLK %" PRIu64
		    ", FRAG %02zx, FEC: %s\n",
		    seq, pktlen, blk->index, blk->fragment_to_send,
		    is_fec ? "YES" : "NO");

	rx_log_frame(ctx, blk->index, blk->fragment_to_send,
	    (uint8_t *)(hdr + 1), pktlen);

	return;
}

static void
send_data_any(struct rx_context *ctx, struct rbuf_block *blk, bool stale,
    bool recovered)
{
	while (blk->fragment_to_send < ctx->fec_k) {
		size_t len = blk->fragment_len[blk->fragment_to_send];

		if (len > 0 || recovered) {
			send_data_one(ctx, blk);
			blk->fragment_to_send++;
			continue;
		}

		if (!stale)
			break;

		blk->fragment_to_send++;
	}
}

static inline void
send_data_seq(struct rx_context *ctx, struct rbuf_block *blk)
{
	return send_data_any(ctx, blk, false, false);
}

static inline void
send_data_stale(struct rx_context *ctx, struct rbuf_block *blk)
{
	return send_data_any(ctx, blk, true, false);
}

static inline void
send_data_recovered(struct rx_context *ctx, struct rbuf_block *blk)
{
	return send_data_any(ctx, blk, false, true);
}

static void
purge_stale(struct rx_context *ctx, struct rbuf_block *blk)
{
	while (!rbuf_block_is_front(blk)) {
		struct rbuf_block *stale = rbuf_get_front(blk->rbuf);

		send_data_stale(ctx, stale);
		rbuf_free_block(stale);
	}
}

static void
data_recovery(struct rx_context *ctx, struct rbuf_block *blk)
{
	int i, j, k;
	const uint8_t **in;
	uint8_t **out;
	unsigned *index;
	size_t pktsiz = 0;

	in = alloca(sizeof(uint8_t *) * ctx->fec_k);
	out = alloca(sizeof(uint8_t *) *
	    (ctx->fec_n - ctx->fec_k));
	index = alloca(sizeof(unsigned) * ctx->fec_k);
	if (!in || !out || !index) {
		p_err("insufficient stack.\n");
		exit(0);
	}

	j = ctx->fec_k;
	k = 0;
	for (i = 0; i < ctx->fec_k; i++) {
		if (blk->fragment_len[i]) {
			in[i] = blk->fragment[i];
			index[i] = i;
		}
		else {
			// look for available parity
			while (!blk->fragment_len[j])
				j++;
			if (pktsiz < blk->fragment_len[j])
				pktsiz = blk->fragment_len[j];
			in[i] = blk->fragment[j];
			out[k++] = blk->fragment[i];
			index[i] = j++;
		}
	}

	fec_wfb_apply(&ctx->fec, in, out, index, pktsiz);
}

static int
data_add(struct rx_context *ctx, struct rbuf_block *blk)
{
	if (rbuf_block_is_front(blk)) {
		// cut through sequencial data.
		send_data_seq(ctx, blk);

		if (blk->fragment_to_send == ctx->fec_k) {
			// all data received. we can drop parity frames.
			rbuf_free_block(blk);
			return 0;
		}
	}
	else {
		// new block is arrived. let's forget old blocks, because
		// we prefer latency to processing reordering.
		purge_stale(ctx, blk);
	}

	assert(rbuf_block_is_front(blk));

	if (blk->fragment_to_send < ctx->fec_k &&
	    blk->fragment_used == ctx->fec_k &&
	    !wfb_options.no_fec) {
		// some frames are lost, but we can recover those using FEC.
		int fec_count = 0;
		int i;
	
		for (i = blk->fragment_to_send; i < ctx->fec_k; i++) {
			if (blk->fragment_len[i] == 0)
				fec_count++;
		}
		if (fec_count) {
			p_debug("Recover %d frames using FEC\n", fec_count);
			data_recovery(ctx, blk);
		}

		// the block is completed, or recovered now.
		send_data_recovered(ctx, blk);
		rbuf_free_block(blk);

		return 0;
	}

	return 0;
}

int
rx_data(struct rx_context *ctx)
{
	struct rbuf_block *blk;
	unsigned long long plain_len;
	uint8_t *fragment_data;
	uint8_t fragment_idx;

	assert(ctx);
	assert(ctx->wfb.hdr);
	assert(ctx->wfb.pktlen);

	if (!ctx->has_session_key)
		return 0;

	assert(ctx->rx_ring);
	
	if (ctx->wfb.pktlen < MIN_DATA_PACKET_LEN) {
		p_err("Frame too short\n");
		return -1;
	}
	if (ctx->wfb.block_idx > MAX_BLOCK_IDX) {
		p_err("Block index out of range.\n");
		return -1;
	}
	if (ctx->wfb.fragment_idx >= ctx->fec_n) {
		p_err("Fragment index out of range.\n");
		return -1;
	}
	rx_log_frame(ctx, ctx->wfb.block_idx, ctx->wfb.fragment_idx, NULL, 0);

	fragment_idx = ctx->wfb.fragment_idx;

	blk = rbuf_get_block(ctx->rx_ring, ctx->wfb.block_idx);
	if (blk == NULL)
		return 0; // the frame is out of window. silent discard.
	if (blk->fragment_len[fragment_idx] != 0)
		return 0; // duplicated frame. silent discard.
	
	fragment_data = blk->fragment[fragment_idx];
	plain_len = ctx->rx_ring->fragment_size;

	if (crypto_wfb_data_decrypt(fragment_data, &plain_len,
	    (uint8_t *)ctx->wfb.hdr, ctx->wfb.pktlen, ctx->wfb.hdrlen,
	    (uint8_t *)ctx->wfb.nonce) < 0) {
		// invalidate session
		rx_context_dump(ctx);
		ctx->has_session_key = false;
		return -1;
	}

	// need to clear rest of buffer to perform FEC.
	memset(fragment_data + plain_len, 0,
	    ctx->rx_ring->fragment_size - plain_len);
	blk->fragment_len[fragment_idx] = plain_len;
	blk->fragment_used++;

	return data_add(ctx, blk);
}
