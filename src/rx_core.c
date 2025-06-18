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
#include "rx_log.h"
#include "util_msg.h"

int
rx_context_initialize(struct rx_context *ctx, uint32_t channel_id)
{
	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->channel_id = channel_id;

	return 0;
}

void
rx_context_deinitialize(struct rx_context *ctx)
{
	assert(ctx);

	if (ctx->rx_ring) {
		rbuf_free(ctx->rx_ring);
		ctx->rx_ring = NULL;
	}
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
		wfb_stats.decoded_frames++;
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
	uint32_t flags = 0;
	int iovcnt = 2;
	int i;

	if (ctx->n_mirror_handler <= 0)
		return;

	if (data == NULL || size == 0) {
		flags |= UDP_FLAG_CORRUPT;
		iovcnt = 1;
	}

	ctx->udp.freq = ctx->freq;
	ctx->udp.dbm = ctx->dbm;
	ctx->udp.flags = flags;
	udp_frame_build(&ctx->udp);

	iov[0].iov_base = ctx->udp.hdr;
	iov[0].iov_len = ctx->udp.hdrlen;
	iov[1].iov_base = data;
	iov[1].iov_len = size;


	for (i = 0; i < ctx->n_mirror_handler; i++) {
		if (ctx->mirror_handler[i].func == NULL)
			continue;

		ctx->mirror_handler[i].func(iov, iovcnt,
		    ctx->mirror_handler[i].arg);
		wfb_stats.mirrored_frames++;
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
	if (parsed < 0) {
		wfb_stats.pcap_libpcap_frame_error++;
		return -1;
	}
	rxbuf += parsed;
	rxlen -= parsed;

	parsed = radiotap_frame_parse(rxbuf, rxlen, &ctx->radiotap);
	if (parsed < 0) {
		wfb_stats.pcap_radiotap_frame_error++;
		return -1;
	}
	if (ctx->radiotap.bad_fcs) {
		/* just notify 'detected something'. */
		rx_mirror_frame(ctx, NULL, 0);
		wfb_stats.pcap_bad_fcs++;
		return -1;
	}
	rxbuf += parsed;
	rxlen -= parsed;
	ctx->freq = ctx->radiotap.freq;
	ctx->dbm = ctx->radiotap.dbm;
	if (ctx->radiotap.has_fcs)
		rxlen -= 4; // Strip FCS from tail.
	
	parsed = ieee80211_frame_parse(rxbuf, rxlen, &ctx->ieee80211);
	if (parsed < 0) {
		wfb_stats.pcap_80211_frame_error++;
		return -1;
	}
	rxbuf += parsed;
	rxlen -= parsed;
	if (ctx->channel_id && ctx->channel_id != ctx->ieee80211.channel_id) {
		wfb_stats.pcap_invalid_channel_id++;
		return -1;
	}

	rx_mirror_frame(ctx, rxbuf, rxlen);

	parsed = wfb_frame_parse(rxbuf, rxlen, &ctx->wfb);
	if (parsed < 0) {
		wfb_stats.pcap_wfb_frame_error++;
		return -1;
	}
	rxbuf += parsed;
	rxlen -= parsed;
	wfb_stats.pcap_accept++;

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
	if (parsed < 0) {
		wfb_stats.mc_udp_frame_error++;
		return -1;
	}
	rxbuf += parsed;
	rxlen -= parsed;
	ctx->freq = ctx->udp.freq;
	ctx->dbm = ctx->udp.dbm;
	if (ctx->udp.flags & UDP_FLAG_CORRUPT) {
		wfb_stats.mc_udp_corrupted_frames++;
		rx_log_corrupt(ctx);
	}

	parsed = wfb_frame_parse(rxbuf, rxlen, &ctx->wfb);
	if (parsed < 0) {
		wfb_stats.mc_udp_wfb_frame_error++;
		return -1;
	}
	rxbuf += parsed;
	rxlen -= parsed;
	wfb_stats.mc_accept++;

	return rx_wfb(ctx);
}
