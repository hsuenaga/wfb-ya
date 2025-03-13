#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include "rx_core.h"
#include "rx_session.h"
#include "rx_data.h"
#include "util_log.h"

int
rx_context_init(struct rx_context *ctx, uint32_t channel_id,
    void (*decode)(uint8_t *data, size_t size, void *arg), void *decode_arg,
    void (*mirror)(uint8_t *data, size_t size, void *arg), void *mirror_arg)
{
	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->decode = decode;
	ctx->decode_arg = decode_arg;
	ctx->mirror = mirror;
	ctx->mirror_arg = mirror_arg;
	ctx->channel_id = channel_id;

	return 0;
}

void
rx_context_dump(struct rx_context *ctx)
{
	int i;

	assert(ctx);

	pcap_context_dump(&ctx->pcap);
	radiotap_context_dump(&ctx->radiotap);
	ieee80211_context_dump(&ctx->ieee80211);
	wfb_context_dump(&ctx->wfb);

	p_info("Block Header: %p\n", ctx->wfb.hdr);
	p_info("Block Header Length: %zu\n", ctx->wfb.hdrlen);
	p_info("Block Length: %zu\n", ctx->wfb.pktlen);
	p_info("Cipher: %p\n", ctx->wfb.cipher);
	p_info("Cipher Length: %llu\n", ctx->wfb.cipherlen);
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

static int
rx_wfb(struct rx_context *ctx)
{
	switch (ctx->wfb.hdr->packet_type) {
		case WFB_PACKET_SESSION:
			return rx_session(ctx);
		case WFB_PACKET_DATA:
			return rx_data(ctx);
		default:
			break;
	}

	return 0;
}

int
rx_frame_pcap(struct rx_context *ctx, void *rxbuf, size_t rxlen)
{
	ssize_t parsed;

	assert(rxbuf);
	assert(ctx);

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
	if (ctx->channel_id && ctx->channel_id != ctx->ieee80211.channel_id)
		return -1;

	if (ctx->mirror)
		ctx->mirror(rxbuf, rxlen, ctx->mirror_arg);

	parsed = wfb_frame_parse(rxbuf, rxlen, &ctx->wfb);
	if (parsed < 0)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;

	return rx_wfb(ctx);
}

int
rx_frame_udp(struct rx_context *ctx, void *rxbuf, size_t rxlen)
{
	ssize_t parsed;

	assert(rxbuf);
	assert(ctx);

	parsed = wfb_frame_parse(rxbuf, rxlen, &ctx->wfb);
	if (parsed < 0)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;

	return rx_wfb(ctx);
}
