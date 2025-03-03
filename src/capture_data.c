#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <alloca.h>

#include "frame_wfb.h"
#include "crypto_wfb.h"
#include "capture_core.h"
#include "capture_data.h"
#include "util_rbuf.h"
#include "util_log.h"

static uint8_t _plain[MAX_DATA_PACKET_SIZE];
static unsigned long long _plain_len;

static void
send_data_k(struct rbuf_block *blk, int k, bool accept_partial, bool force)
{
	while (blk->fragment_to_send < k) {
		struct wfb_data_hdr *hdr;
		size_t len = blk->fragment_len[blk->fragment_to_send];
		
		if (!len && !force) {
			if (accept_partial) {
				blk->fragment_to_send++;
				continue;
			}
			break;
		}

	       	hdr = (struct wfb_data_hdr *)
			blk->fragment[blk->fragment_to_send];
		p_info("Packet Received: %d bytes, BLK %" PRIu64
		    ", FRAG %02x, FEC: %s\n",
		    be16toh(hdr->packet_size), blk->index,
		    blk->fragment_to_send,
		    (len == 0) ? "YES" : "NO");

		blk->fragment_to_send++;
	}
}

static void
purge_stale(struct capture_context *ctx, struct rbuf_block *blk)
{
	while (!rbuf_block_is_front(blk)) {
		struct rbuf_block *stale = rbuf_get_front(blk->rbuf);

		send_data_k(stale, ctx->fec_k, true, false);
		rbuf_free_block(stale);
	}
}

static void
data_recovery(struct capture_context *ctx, struct rbuf_block *blk)
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
data_flush(struct capture_context *ctx, struct rbuf_block *blk)
{
	if (rbuf_block_is_front(blk)) {
		send_data_k(blk, ctx->fec_k, false, false);

		if (blk->fragment_to_send == ctx->fec_k) {
			// drop parity frames.
			rbuf_free_block(blk);
			return 0;
		}
	}

	if (blk->fragment_to_send < ctx->fec_k &&
	    blk->fragment_used == ctx->fec_k) {
		// the block is completed, or can be recovered by FEC.
		int fec_count = 0;
		int i;
	
		purge_stale(ctx, blk);

		for (i = blk->fragment_to_send; i < ctx->fec_k; i++) {
			if (blk->fragment_len[i] == 0)
				fec_count++;
		}
		if (fec_count) {
			p_info("Recover %d frames using FEC\n", fec_count);
			data_recovery(ctx, blk);
		}

		// the block is completed, or recovered now.
		send_data_k(blk, ctx->fec_k, false, true);
		rbuf_free_block(blk);

		return 0;
	}

	// queue frames until at least one block to be completed.
	return 0;
}


int
capture_data(struct capture_context *ctx)
{
	struct rbuf_block *blk;

	if (!ctx->payload || !ctx->payload_len || !ctx->rx_ring)
		return -1; // XXX: assert

	if (!ctx->has_session_key)
		return 0;
	
	if (ctx->payload_len < MIN_DATA_PACKET_LEN) {
		p_err("Frame too short\n");
		return -1;
	}

	_plain_len = sizeof(_plain);
	if (crypto_wfb_data_decrypt(_plain, &_plain_len,
	    ctx->payload, ctx->payload_len, ctx->payload_len - ctx->cipher_len,
	    (uint8_t *)&ctx->wfb.nonce.data) < 0) {
		// invalidate session
		capture_context_dump(ctx);
		ctx->has_session_key = false;
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
	blk = rbuf_get_block(ctx->rx_ring, ctx->wfb.block_idx);
	if (blk == NULL)
		return -1; // the frame is out of window.
	if (blk->fragment_len[ctx->wfb.fragment_idx] != 0)
		return -1; // duplicated frame.
	// XXX: don't copy, just specify this buffer in decrypto.
	memset(blk->fragment[ctx->wfb.fragment_idx], 0,
	    blk->rbuf->fragment_size);
	memcpy(blk->fragment[ctx->wfb.fragment_idx], _plain, _plain_len);
	blk->fragment_len[ctx->wfb.fragment_idx] = _plain_len;
	blk->fragment_used++;

	return data_flush(ctx, blk);
}
