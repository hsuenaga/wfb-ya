#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

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
	struct wfb_session_data *data;
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
	data = (struct wfb_session_data *)ctx->wfb.cipher;
	epoch = be64toh(data->epoch);

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
	ctx->epoch = epoch;
	ctx->fec_type = data->fec_type;
	ctx->fec_k = data->fec_k;
	ctx->fec_n = data->fec_n;
	// XXX: ctx->session_key is not reuiqred in the fact.
	memcpy(ctx->session_key, data->session_key, sizeof(ctx->session_key));
	crypto_wfb_session_key_set(data->session_key,
	    sizeof(data->session_key));
	ctx->has_session_key = true;

	rx_session_dump(ctx);

	return 0;
}
