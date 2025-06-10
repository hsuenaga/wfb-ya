#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/uio.h>

#include "compat.h"

#include "rx_core.h"
#include "rx_session.h"
#include "rx_data.h"
#include "util_msg.h"

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
    void (*mirror)(struct iovec *iov, int iovcnt, void *arg), void *mirror_arg)
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
	struct iovec iov[2];
	int i;

	if (ctx->n_mirror_handler <= 0)
		return;

	ctx->udp.freq = ctx->freq;
	ctx->udp.dbm = ctx->dbm;
	udp_frame_build(&ctx->udp);

	iov[0].iov_base = ctx->udp.hdr;
	iov[0].iov_len = ctx->udp.hdrlen;
	iov[1].iov_base = data;
	iov[1].iov_len = size;


	for (i = 0; i < ctx->n_mirror_handler; i++) {
		if (ctx->mirror_handler[i].func == NULL)
			continue;

		ctx->mirror_handler[i].func(iov, 2, ctx->mirror_handler[i].arg);
	}
}

void
rx_log_frame(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, uint8_t *data, size_t size)
{
	struct rx_log_header hd;
	struct rx_logger *log = &ctx->log_handler;
	struct timespec ts;

	if (log->fp == NULL)
		return;
	if (data == NULL)
		size = 0;
	if (size == 0)
		data = NULL;

	hd.seq = block_idx * ctx->fec_n + fragment_idx;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		hd.ts = ts;
	}
	else {
		hd.ts.tv_sec = 0;
		hd.ts.tv_nsec = 0;
	}
	hd.block_idx = block_idx;
	hd.fragment_idx = fragment_idx;;
	hd.fec_k = ctx->fec_k;
	hd.fec_n = ctx->fec_n;
	hd.size = size;
	if (size == 0) {
		if (ctx->rx_src.sin6_family == AF_INET6)
			hd.rx_src = ctx->rx_src;
		hd.freq = ctx->freq;
		hd.dbm = ctx->dbm;
	}
	else {
		hd.rx_src.sin6_family = AF_UNSPEC;
		hd.freq = 0;
		hd.dbm = INT16_MIN;
	}

	p_debug("Packet Log: SEQ %llu, BLK %llu, FRAG %u, SIZE %lu\n",
	    hd.seq, hd.block_idx, hd.fragment_idx, size);

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
	if (wfb_options.log_file == NULL) {
		log->fp = NULL;
		return;
	}

	log->fp = fopen(wfb_options.log_file, "w");
	if (log->fp) {
		p_debug("New LOG File: %s\n", wfb_options.log_file);
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

	ctx->rx_src.sin6_family = AF_UNSPEC;

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
	ctx->freq = ctx->radiotap.freq;
	ctx->dbm = ctx->radiotap.dbm;
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

	// NOTE: ctx->rx_src is set by net_inet6.c

	parsed = udp_frame_parse(rxbuf, rxlen, &ctx->udp);
	if (parsed < 0)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;
	ctx->freq = ctx->udp.freq;
	ctx->dbm = ctx->udp.dbm;

	parsed = wfb_frame_parse(rxbuf, rxlen, &ctx->wfb);
	if (parsed < 0)
		return -1;
	rxbuf += parsed;
	rxlen -= parsed;

	return rx_wfb(ctx);
}
