#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <alloca.h>
#include <assert.h>

#include "frame_wfb.h"
#include "crypto_wfb.h"
#include "rx_core.h"
#include "rx_data.h"
#include "util_rbuf.h"
#include "util_log.h"

static void
send_data_one(struct rbuf_block *blk)
{
	struct wfb_data_hdr *hdr;
	size_t len;

	hdr = (struct wfb_data_hdr *)blk->fragment[blk->fragment_to_send];
	len = blk->fragment_len[blk->fragment_to_send];

	p_info("Received: %d bytes, BLK %" PRIu64 ", FRAG %02x, FEC: %s\n",
	    be16toh(hdr->packet_size), blk->index, blk->fragment_to_send,
	    (len == 0) ? "YES" : "NO");

	// TODO: callback here.
	
	return;
}

static void
send_data_k(struct rbuf_block *blk, int k, bool stale, bool recovered)
{
	while (blk->fragment_to_send < k) {
		size_t len = blk->fragment_len[blk->fragment_to_send];

		if (len > 0 || recovered) {
			send_data_one(blk);
			blk->fragment_to_send++;
			continue;
		}

		if (!stale)
			break;

		blk->fragment_to_send++;
	}
}

static inline void
send_data_seq(struct rbuf_block *blk, int k)
{
	return send_data_k(blk, k, false, false);
}

static inline void
send_data_stale(struct rbuf_block *blk, int k)
{
	return send_data_k(blk, k, true, false);
}

static inline void
send_data_recovered(struct rbuf_block *blk, int k)
{
	return send_data_k(blk, k, false, true);
}

static void
purge_stale(struct rx_context *ctx, struct rbuf_block *blk)
{
	while (!rbuf_block_is_front(blk)) {
		struct rbuf_block *stale = rbuf_get_front(blk->rbuf);

		send_data_stale(stale, ctx->fec_k);
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
		send_data_seq(blk, ctx->fec_k);

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
	    blk->fragment_used == ctx->fec_k) {
		// some frames are lost, but we can recover those using FEC.
		int fec_count = 0;
		int i;
	
		for (i = blk->fragment_to_send; i < ctx->fec_k; i++) {
			if (blk->fragment_len[i] == 0)
				fec_count++;
		}
		if (fec_count) {
			p_info("Recover %d frames using FEC\n", fec_count);
			data_recovery(ctx, blk);
		}

		// the block is completed, or recovered now.
		send_data_recovered(blk, ctx->fec_k);
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
