#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#include <string.h>

#include "frame_ieee80211.h"
#include "util_log.h"

static inline int
get_version(uint16_t frame_control)
{
	return (frame_control & 0x03);
}

static inline int
get_frame_type(uint16_t frame_control)
{
	return ((frame_control >> 2) & 0x3);
}

static inline int
get_frame_subtype(uint16_t frame_control)
{
	return ((frame_control >> 4) & 0xf);
}

static inline uint16_t
get_signature(uint8_t *addr)
{
	uint16_t v;

	memcpy(&v, &addr[0], sizeof(v));

	return be16toh(v);
}

static inline uint32_t
get_channel_id(uint8_t *addr)
{
	uint32_t v;

	memcpy(&v, &addr[2], sizeof(v));

	return be32toh(v);
}

void
ieee80211_context_dump(const struct ieee80211_context *ctx)
{
	assert(ctx);

	p_info("DST: %s\n", s_mac48(ctx->hdr->u.base3.addr1));
	p_info("SRC: %s\n", s_mac48(ctx->hdr->u.base3.addr2));
	p_info("BSS: %s\n", s_mac48(ctx->hdr->u.base3.addr3));
	p_info("WFB SIGNATURE: 0x%04x\n", ctx->wfb_signature);
	p_info("WFB Channel ID: 0x%06x\n", ctx->channel_id);
}

ssize_t
ieee80211_frame_parse(void *data, size_t size, struct ieee80211_context *ctx)
{
	uint16_t frame_control;
	int version, type, subtype;

	assert(data);
	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->hdr = (struct ieee80211_header *)data;

	if (size < sizeof(frame_control)) {
		p_err("Frame too short\n");
		return -1;
	}
	memcpy(&frame_control, &ctx->hdr->frame_control, sizeof(frame_control));
	frame_control = le16toh(frame_control);
	version = get_version(frame_control);
	type = get_frame_type(frame_control);
	subtype = get_frame_subtype(frame_control);

	if (version != 0) {
		p_err("Unknown version\n");
		return -1;
	}
	switch (type) {
		case FTYPE_DATA:
			break;
		case FTYPE_MGMT:
		case FTYPE_CTRL:
		case FTYPE_EXT:
		default:
			p_err("Unsupported frame type(%d)\n", type);
			return -1;

	}
	switch (subtype) {
		case DTYPE_DATA:
			ctx->hdrlen = IEEE80211_DATA_HDRLEN;
			break;
		case DTYPE_NULL:
		case DTYPE_QOS:
		case DTYPE_QOS_ACK:
		case DTYPE_QOS_POLL:
		case DTYPE_QOS_ACK_POLL:
		case DTYPE_QOS_NULL:
		case DTYPE_QOS_NULL_POLL:
		case DTYPE_QOS_NULL_ACK_POLL:
		default:
			p_err("Unsupported frame subtype(%d)\n", subtype);
			return -1;
	}

	if (size < ctx->hdrlen) {
		p_err("Frame too short");
		return -1;
	}

	ctx->wfb_signature = get_signature(ctx->hdr->u.base3.addr2);
	ctx->channel_id = get_channel_id(ctx->hdr->u.base3.addr2);

	switch (ctx->wfb_signature) {
		case WFG_SIG:
			break;
		default:
			p_err("Unknown signature\n");
			return -1;
	}

	return ctx->hdrlen;
}
