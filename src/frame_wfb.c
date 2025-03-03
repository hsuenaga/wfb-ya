#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame_wfb.h"
#include "crypto_wfb.h"
#include "util_log.h"

static const char *
s_wfb_packet_type(uint8_t type)
{
	switch (type) {
		case WFB_PACKET_DATA:
			return "DATA";
		case WFB_PACKET_SESSION:
			return "SESSION";
		default:
			break;
	}

	return "Unknown";
}

static void
dump_data_nonce(const struct wfb_context *ctx)
{
	int i;

	p_info("WFB Data Nonce: 0x");
	for (i = 0; i < sizeof(ctx->nonce.data); i++) {
		p_info("%02x", ctx->nonce.data[i]);
	}
	p_info("\n");
}

static void
dump_session_nonce(const struct wfb_context *ctx)
{
	int i;

	p_info("WFB Session Nonce: 0x");
	for (i = 0; i < sizeof(ctx->nonce.session); i++) {
		p_info("%02x", ctx->nonce.session[i]);
	}
	p_info("\n");
}

void
wfb_context_dump(const struct wfb_context *ctx)
{
	p_info("WFB Pcaket Type: %s\n", s_wfb_packet_type(ctx->packet_type));
	switch (ctx->packet_type) {
		case WFB_PACKET_DATA:
			dump_data_nonce(ctx);
			break;
		case WFB_PACKET_SESSION:
			dump_session_nonce(ctx);
			break;
		default:
			break;
	}
	p_info("WFB Data Block Index: %" PRIu64 "\n", ctx->block_idx);
	p_info("WFB Data Fragment Index: %u\n", ctx->fragment_idx);
}

ssize_t
wfb_frame_parse(void *data, size_t size, struct wfb_context *ctx)
{
	struct wfb_ng_hdr *hdr = (struct wfb_ng_hdr *)data;
	ssize_t hdrlen = 0;
	uint64_t v64;

	if (size < sizeof(hdr->packet_type)) {
		p_err("Frame too short\n");
		return -1;
	}
	if (size > MAX_DATA_PACKET_SIZE) {
		p_err("Frame too long\n");
		return -1;
	}
	ctx->packet_type = hdr->packet_type;

	switch (ctx->packet_type) {
		case WFB_PACKET_DATA:
			if (size < WFB_DATA_BLOCK_HDRLEN) {
				p_err("Frame too short\n");
				return -1;
			}
			memcpy(ctx->nonce.data, hdr->u.data.nonce,
			    sizeof(ctx->nonce.data));
			memcpy(&v64, hdr->u.data.nonce, sizeof(v64));
			v64 = be64toh(v64);
			ctx->block_idx = v64 >> 8;
			ctx->fragment_idx = (uint8_t)(v64 & 0xff);
			hdrlen = WFB_DATA_BLOCK_HDRLEN;
			break;
		case WFB_PACKET_SESSION:
			if (size < WFB_SESSION_BLOCK_HDRLEN) {
				p_err("Frame too short\n");
				return -1;
			}
			memcpy(ctx->nonce.session, hdr->u.session.nonce,
			    sizeof(ctx->nonce.session));
			hdrlen = WFB_SESSION_BLOCK_HDRLEN;
			break;
		default:
			p_err("Unknown packet type\n");
			return -1;
	}

	return hdrlen;
}
