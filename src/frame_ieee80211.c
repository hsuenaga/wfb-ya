#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <string.h>

#include "frame_ieee80211.h"
#include "util_log.h"

static int
r_version(struct ieee80211_header *hdr)
{
	uint16_t v = le16toh(hdr->frame_control);

	return (v & 0x03);
}

static int
r_ftype(struct ieee80211_header *hdr)
{
	uint16_t v = le16toh(hdr->frame_control);

	return ((v >> 2) & 0x3);
}

static int
r_subtype(struct ieee80211_header *hdr)
{
	uint16_t v = le16toh(hdr->frame_control);

	return ((v >> 4) & 0xf);
}

static uint16_t
r_signature(struct ieee80211_header *hdr)
{
	uint16_t v;

	memcpy(&v, &hdr->u.base3.addr2[0], sizeof(v));

	return be16toh(v);
}

static uint32_t
r_channel_id(struct ieee80211_header *hdr)
{
	uint32_t v;

	memcpy(&v, &hdr->u.base3.addr2[2], sizeof(v));

	return (be32toh(v)); // WFG is Big-Endian
}

static uint8_t
r_stream_id(struct ieee80211_header *hdr)
{
	return (hdr->u.base3.addr2[5]);
}

void
ieee80211_context_dump(const struct ieee80211_context *ctx)
{
	p_info("DST: %02x-%02x-%02x-%02x-%02x-%02x\n",
		ctx->dst[0], ctx->dst[1], ctx->dst[2],
		ctx->dst[3], ctx->dst[4], ctx->dst[5]);
	p_info("SRC: %02x-%02x-%02x-%02x-%02x-%02x\n",
		ctx->src[0], ctx->src[1], ctx->src[2],
		ctx->src[3], ctx->src[4], ctx->src[5]);
	p_info("BSS: %02x-%02x-%02x-%02x-%02x-%02x\n",
		ctx->bss[0], ctx->bss[1], ctx->bss[2],
		ctx->bss[3], ctx->bss[4], ctx->bss[5]);
	p_info("WFB SIGNATURE: 0x%04x\n", ctx->wfb_signature);
	p_info("WFB Channel ID: 0x%06x\n", ctx->channel_id);
	p_info("WFB Stream ID: 0x%02x\n", ctx->stream_id);
}

ssize_t
ieee80211_frame_parse(void *data, size_t size, struct ieee80211_context *ctx)
{
	struct ieee80211_header *hdr = (struct ieee80211_header *)data;
	size_t hdrlen = 0;

	if (size < 4) {
		p_err("Frame too short\n");
		return -1;
	}
	if (r_version(hdr) != 0) {
		p_err("Unknown version\n");
		return -1;
	}
	switch (r_ftype(hdr)) {
		case FTYPE_DATA:
			break;
		case FTYPE_MGMT:
		case FTYPE_CTRL:
		case FTYPE_EXT:
		default:
			p_err("Unknown frame type(%d)\n", r_ftype(hdr));
			return -1;

	}
	switch (r_subtype(hdr)) {
		case DTYPE_DATA:
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
			p_err("Unknown frame subtype\n");
			return -1;
	}
	hdrlen = IEEE80211_DATA_HDRLEN;
	if (size < hdrlen) {
		p_err("Frame too short");
		return -1;
	}
	memcpy(ctx->dst, hdr->u.base3.addr1, sizeof(ctx->dst));
	memcpy(ctx->src, hdr->u.base3.addr2, sizeof(ctx->dst));
	memcpy(ctx->bss, hdr->u.base3.addr3, sizeof(ctx->dst));
	ctx->wfb_signature = r_signature(hdr);
	ctx->channel_id = r_channel_id(hdr);
	ctx->stream_id = r_stream_id(hdr);

	switch (ctx->wfb_signature) {
		case WFG_SIG:
			break;
		default:
			p_err("Unknown signature\n");
			return -1;
	}

	return hdrlen;
}
