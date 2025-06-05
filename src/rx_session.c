#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "compat.h"

#include "crypto_wfb.h"
#include "rx_core.h"
#include "rx_session.h"
#include "frame_wfb.h"
#include "util_log.h"

static void
rx_session_dump(struct rx_context *ctx)
{
	assert(ctx);

	p_info("-- New Session --\n");
	p_info("epoch: %" PRIu64 "\n", ctx->epoch);
	p_info("fec_type: %u\n", ctx->fec_type);
	p_info("fec_k: %u\n", ctx->fec_k);
	p_info("fec_n: %u\n", ctx->fec_n);
	p_info("session_key: 0x%s\n",
	    s_binary(ctx->session_key, sizeof(ctx->session_key)));
}

int
rx_session(struct rx_context *ctx)
{
	struct wfb_session_hdr *hdr;
	uint64_t epoch;
	int r;

	assert(ctx);
	assert(ctx->wfb.cipher);
	assert(ctx->wfb.nonce);

	if (ctx->wfb.pktlen < MIN_SESSION_PACKET_LEN) {
		p_err("Frame too short\n");
		return -1;
	}

	r = crypto_wfb_session_decrypt(ctx->wfb.cipher, ctx->wfb.cipher,
	    ctx->wfb.cipherlen, ctx->wfb.nonce);
	if (r < 0)
		return -1;
	hdr = (struct wfb_session_hdr *)ctx->wfb.cipher;
	epoch = be64toh(hdr->epoch);

	if (epoch < ctx->epoch) {
		p_err("Invalid Epoch\n");
		// XXX: ... But how to recover from Tx reboot???
		return -1;
	}
	if (ctx->has_session_key && epoch == ctx->epoch) {
		// No rekeying required. drop the frame siliently.
		return 0;
	}

	// Start rekeying. We need strict error checking before accepting.
	ctx->has_session_key = false;
	if (ctx->channel_id && ctx->channel_id != be32toh(hdr->channel_id)) {
		p_err("Channel ID mismach\n");
		return -1;
	}
	switch (hdr->fec_type) {
		case WFB_FEC_VDM_RS:
			break;
		default:
			p_err("Unsupported FEC type\n");
			return -1;
	}
	if (hdr->fec_n < 1) {
		p_err("Invalid FEC N\n");
		return -1;
	}
	if (hdr->fec_k < 1 || hdr->fec_k > hdr->fec_n) {
		p_err("Invalid FEC K\n");
		return -1;
	}
	if (fec_wfb_new(&ctx->fec,
	    hdr->fec_type, hdr->fec_k, hdr->fec_n) < 0) {
		p_err("Cannot Initialize FEC\n");
		return -1;
	}
	if (ctx->rx_ring)
		rbuf_free(ctx->rx_ring);
	ctx->rx_ring = rbuf_alloc(RX_RING_SIZE, MAX_FEC_PAYLOAD, hdr->fec_n);
	if (ctx->rx_ring == NULL) {
		p_err("Cannot Initialize Rx Buffer\n");
		return -1;
	}
	ctx->epoch = epoch;
	ctx->fec_type = hdr->fec_type;
	ctx->fec_k = hdr->fec_k;
	ctx->fec_n = hdr->fec_n;
	memcpy(ctx->session_key, hdr->session_key, sizeof(ctx->session_key));
	crypto_wfb_session_key_set(hdr->session_key, sizeof(hdr->session_key));
	ctx->has_session_key = true;

	rx_session_dump(ctx);

	rx_log_create(ctx);

	return 0;
}
