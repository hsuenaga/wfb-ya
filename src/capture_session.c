#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "crypto_wfb.h"
#include "capture_core.h"
#include "capture_session.h"
#include "frame_wfb.h"
#include "util_log.h"

static void
capture_session_dump(struct capture_context *ctx)
{
	int i;

	p_info("-- New Session --\n");
	p_info("epoch: %" PRIu64 "\n", ctx->epoch);
	p_info("fec_type: %" PRIu8 "\n", ctx->fec_type);
	p_info("fec_k: %" PRIu8 "\n", ctx->fec_k);
	p_info("fec_n: %" PRIu8 "\n", ctx->fec_n);
	p_info("session_key: 0x");
	for (i = 0; i < crypto_aead_chacha20poly1305_KEYBYTES; i++) {
		p_info("%02" PRIx8, ctx->session_key[i]);
	}
	p_info("\n");
}

int
capture_session(struct capture_context *ctx)
{
	struct wfb_session_data *data = (struct wfb_session_data *)ctx->cipher;
	int r;

	if (!ctx->payload)
		return -1;
	if (ctx->payload_len < MIN_SESSION_PACKET_LEN) {
		p_err("Frame too short\n");
		return -1;
	}

	r = crypto_wfb_session_decrypt(ctx->cipher, ctx->cipher,
	    ctx->cipher_len, ctx->wfb.nonce.session);
	if (r < 0)
		return -1;

	if (be64toh(data->epoch) < ctx->epoch) {
		p_err("Invalid Epoch\n");
		// XXX: ... But how to recover from Tx reboot???
		return -1;
	}
	if (ctx->has_session_key && be64toh(data->epoch) == ctx->epoch) {
		// No rekeying required. drop the frame siliently.
		return 0;
	}

	// Start rekeying. We need strict error checking before accepting.
	ctx->has_session_key = false;
	if (ctx->ieee80211.channel_id != be32toh(data->channel_id)) {
		p_err("Channel ID mismach\n");
		return -1;
	}
	switch (data->fec_type) {
		case WFB_FEC_VDM_RS:
			break;
		default:
			p_err("Unsupported FEC type\n");
			return -1;
	}
	if (data->fec_n < 1) {
		p_err("Invalid FEC N\n");
		return -1;
	}
	if (data->fec_k < 1 || data->fec_k > data->fec_n) {
		p_err("Invalid FEC K\n");
		return -1;
	}
	if (fec_wfb_new(&ctx->fec,
	    data->fec_type, data->fec_k, data->fec_n) < 0) {
		p_err("Cannot Initialize FEC\n");
		return -1;
	}
	if (ctx->rx_ring)
		rbuf_free(ctx->rx_ring);
	ctx->rx_ring = rbuf_alloc(RX_RING_SIZE, MAX_FEC_PAYLOAD, data->fec_n);
	if (ctx->rx_ring == NULL) {
		p_err("Cannot Initialize Rx Buffer\n");
		return -1;
	}
	ctx->epoch = be64toh(data->epoch);
	ctx->fec_type = data->fec_type;
	ctx->fec_k = data->fec_k;
	ctx->fec_n = data->fec_n;
	memcpy(ctx->session_key, data->session_key, sizeof(ctx->session_key));
	crypto_wfb_session_key_set(ctx->session_key, sizeof(ctx->session_key));
	ctx->has_session_key = true;

	capture_session_dump(ctx);

	return 0;
}
