#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "compat.h"

#include "rx_core.h"
#include "rx_session.h"
#include "rx_data.h"
#include "util_log.h"

int
rx_context_init(struct rx_context *ctx, uint32_t channel_id)
{
	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->channel_id = channel_id;

	return 0;
}

int
rx_context_set_decode(struct rx_context *ctx,
    void (*decode)(uint8_t *data, size_t size, void *arg), void *decode_arg)
{
	int i;

	assert(ctx);

	for (i = 0; i < RX_MAX_DECODE; i++) {
		if (ctx->decode_handler[i].func != NULL)
			continue;
		ctx->decode_handler[i].func = decode;
		ctx->decode_handler[i].arg = decode_arg;
		ctx->n_decode_handler++;
		return 0;
	}

	return -1;
}

void
rx_decode_frame(struct rx_context *ctx, uint8_t *data, size_t size)
{
	int i;

	if (ctx->n_decode_handler <= 0)
		return;

	for (i = 0; i < ctx->n_decode_handler; i++) {
		if (ctx->decode_handler[i].func == NULL)
			continue;

		ctx->decode_handler[i].func(data, size,
		    ctx->decode_handler[i].arg);
	}
}

int
rx_context_set_mirror(struct rx_context *ctx,
    void (*mirror)(uint8_t *data, size_t size, void *arg), void *mirror_arg)
{
	int i;

	assert(ctx);

	for (i = 0; i < RX_MAX_MIRROR; i++) {
		if (ctx->mirror_handler[i].func != NULL)
			continue;
		ctx->mirror_handler[i].func = mirror;
		ctx->mirror_handler[i].arg = mirror_arg;
		ctx->n_mirror_handler++;
		return 0;
	}

	return -1;
}

void
rx_mirror_frame(struct rx_context *ctx, uint8_t *data, size_t size)
{
	int i;

	if (ctx->n_mirror_handler <= 0)
		return;

	for (i = 0; i < ctx->n_mirror_handler; i++) {
		if (ctx->mirror_handler[i].func == NULL)
			continue;

		ctx->mirror_handler[i].func(data, size,
		    ctx->mirror_handler[i].arg);
	}
}

void
rx_log_frame(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, uint8_t *data, size_t size)
{
	struct rx_log_header hd;
	struct rx_logger *log = &ctx->log_handler;
	struct timespec ts;

	hd.seq = block_idx * ctx->fec_n + fragment_idx;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		hd.ts = ts;
	}
	else {
		hd.ts.tv_sec = 0;
		hd.ts.tv_nsec = 0;
	}
	hd.size = data ? size : 0;
	hd.block_idx = block_idx;
	hd.fragment_idx = fragment_idx;;
	hd.fec_k = ctx->fec_k;
	hd.fec_n = ctx->fec_n;

	p_debug("Packet Log: SEQ %lu, BLK %lu, FRAG %u, SIZE %lu\n",
	    hd.seq, hd.block_idx, hd.fragment_idx, size);

	if (log->fp == NULL)
		return;

	fwrite(&hd, sizeof(hd), 1, log->fp);
	if (data && size > 0) {
		fwrite(data, size, 1, log->fp);
	}
	fflush(log->fp);
}

void
rx_log_create(struct rx_context *ctx)
{
	struct rx_logger *log = &ctx->log_handler;

	if (log->fp)
		fclose(log->fp);

	log->fp = fopen(options.log_file, "w");
	if (log->fp) {
		p_debug("New LOG File: %s\n", options.log_file);
	}
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

	rx_mirror_frame(ctx, rxbuf, rxlen);

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
