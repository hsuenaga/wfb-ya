#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture_core.h"
#include "capture_session.h"
#include "capture_data.h"
#include "util_log.h"

int
capture_context_init(struct capture_context *ctx)
{
	memset(ctx, 0, sizeof(*ctx));

	return 0;
}

void
capture_context_dump(struct capture_context *ctx)
{
	int i;

	pcap_context_dump(&ctx->pcap);
	radiotap_context_dump(&ctx->radiotap);
	ieee80211_context_dump(&ctx->ieee80211);
	wfb_context_dump(&ctx->wfb);

	p_info("Payload: %p\n", ctx->payload);
	p_info("Payload Length: %zu\n", ctx->payload_len);
	p_info("Cipher: %p\n", ctx->cipher);
	p_info("Cipher Length: %zu\n", ctx->cipher_len);
	p_info("Epoch: %" PRIu64 "\n", ctx->epoch);
	p_info("FEC Type: %u\n", ctx->fec_type);
	p_info("FEC K: %u\n", ctx->fec_k);
	p_info("FEC N: %u\n", ctx->fec_n);
	if (ctx->has_session_key) {
		p_info("Session Key : ");
		for (i = 0; i < sizeof(ctx->session_key); i++)
			p_info("%02x", ctx->session_key[i]);
		p_info("\n");
	}
	else {
		p_info("No Session Key.\n");
	}
}

int
capture_frame(void *rxbuf, size_t rxlen, void *arg)
{
	struct capture_context *ctx = (struct capture_context *)arg;
	ssize_t parsed;

	parsed = pcap_frame_parse(rxbuf, rxlen, &ctx->pcap);
	if (parsed < 0)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;

	parsed = radiotap_frame_parse(rxbuf, rxlen, &ctx->radiotap);
	if (parsed < 0 || ctx->radiotap.bad_fcs)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;
	if (ctx->radiotap.has_fcs)
		rxlen -= 4; // Strip FCS from tail.
	
	parsed = ieee80211_frame_parse(rxbuf, rxlen, &ctx->ieee80211);
	if (parsed < 0)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;


	parsed = wfb_frame_parse(rxbuf, rxlen, &ctx->wfb);
	if (parsed < 0)
		return -1;
	ctx->payload = rxbuf;
	ctx->payload_len = rxlen;
	rxbuf += parsed;
	rxlen -= parsed;
	ctx->cipher = rxbuf;
	ctx->cipher_len = rxlen;


	switch (ctx->wfb.packet_type) {
		case WFB_PACKET_SESSION:
			return capture_session(ctx);
		case WFB_PACKET_DATA:
			return capture_data(ctx);
		default:
			break;
	}

	return 0;
}
