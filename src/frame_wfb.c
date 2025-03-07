#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "frame_wfb.h"
#include "crypto_wfb.h"
#include "util_log.h"

static const char *
s_packet_type(uint8_t type)
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

void
wfb_context_dump(const struct wfb_context *ctx)
{
	assert(ctx);

	p_info("WFB Pcaket Type: %s\n", s_packet_type(ctx->hdr->packet_type));
	p_info("WFB Nonce: %s\n", s_binary(ctx->nonce, ctx->noncelen));
	p_info("WFB Data Block Index: %" PRIu64 "\n", ctx->block_idx);
	p_info("WFB Data Fragment Index: %u\n", ctx->fragment_idx);
}

ssize_t
wfb_frame_parse(void *data, size_t size, struct wfb_context *ctx)
{
	uint64_t v64;

	assert(data);
	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->hdr = (struct wfb_ng_hdr *)data;

	if (size < sizeof(ctx->hdr->packet_type)) {
		p_err("Frame too short\n");
		return -1;
	}
	if (size > MAX_DATA_PACKET_SIZE) {
		p_err("Frame too long\n");
		return -1;
	}
	ctx->pktlen = size;

	switch (ctx->hdr->packet_type) {
		case WFB_PACKET_DATA:
			if (size < WFB_DATA_BLOCK_HDRLEN) {
				p_err("Frame too short\n");
				return -1;
			}
			ctx->nonce = ctx->hdr->u.data.nonce;
			ctx->noncelen = sizeof(ctx->hdr->u.data.nonce);
			memcpy(&v64, ctx->nonce, sizeof(v64));
			v64 = be64toh(v64);
			ctx->block_idx = v64 >> 8;
			ctx->fragment_idx = (uint8_t)(v64 & 0xff);
			ctx->hdrlen = WFB_DATA_BLOCK_HDRLEN;
			break;
		case WFB_PACKET_SESSION:
			if (size < WFB_SESSION_BLOCK_HDRLEN) {
				p_err("Frame too short\n");
				return -1;
			}
			ctx->nonce = ctx->hdr->u.session.nonce;
			ctx->noncelen = sizeof(ctx->hdr->u.session.nonce);
			ctx->hdrlen = WFB_SESSION_BLOCK_HDRLEN;
			break;
			break;
		default:
			p_err("Unknown packet type\n");
			return -1;
	}
	if (ctx->pktlen < ctx->hdrlen) {
		p_err("Frame too short\n");
		return -1;
	}
	ctx->cipher = (uint8_t *)ctx->hdr + ctx->hdrlen;
	ctx->cipherlen = ctx->pktlen - ctx->hdrlen;

	return ctx->pktlen;
}
