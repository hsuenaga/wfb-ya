#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "frame_udp.h"
#include "rx_log.h"
#include "util_msg.h"
#include "compat.h"

void
udp_context_dump(struct udp_context *ctx)
{
	assert(ctx);

	p_info("UDP: Freq %u\n", ctx->freq);
	p_info("UDP: dBm %d\n", ctx->dbm);
}

ssize_t
udp_frame_parse(void *data, size_t size, struct udp_context *ctx)
{
	assert(data);
	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->hdr = (struct wfb_udp_header *)data;
	ctx->hdrlen = sizeof(struct wfb_udp_header);

	ctx->freq = ntohs(ctx->hdr->freq);
	ctx->dbm = ntohs(ctx->hdr->dbm);
	if (ctx->dbm < DBM_MIN || ctx->dbm > DBM_MAX)
		ctx->dbm = DBM_INVAL;
	ctx->flags = ntohl(ctx->hdr->flags);

	return ctx->hdrlen;
}

ssize_t
udp_frame_build(struct udp_context *ctx)
{
	assert(ctx);

	memset(&ctx->raw, 0, sizeof(ctx->raw));
	ctx->hdr = &ctx->raw;
	ctx->hdrlen = sizeof(ctx->raw);

	ctx->hdr->freq = htons(ctx->freq);
	if (ctx->dbm >= DBM_MIN && ctx->dbm  <= DBM_MAX) {
		ctx->hdr->dbm = htons(ctx->dbm);
	}
	else {
		int16_t v = DBM_INVAL; 
		ctx->hdr->dbm = htons(v);
	}
	ctx->hdr->flags = htonl(ctx->flags);

	return ctx->hdrlen;
}
