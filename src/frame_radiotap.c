#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <endian.h>

#include <radiotap.h>
#include <radiotap_iter.h>

#include "frame_radiotap.h"
#include "util_log.h"

static uint8_t
r_uint8(struct ieee80211_radiotap_iterator *iter)
{
	return *((uint8_t *)(iter->this_arg));
}

static uint16_t
r_uint16(struct ieee80211_radiotap_iterator *iter)
{
	uint16_t v = *((uint16_t *)(iter->this_arg));

	return le16toh(v);
}

static uint32_t
r_uint32(struct ieee80211_radiotap_iterator *iter)
{
	uint32_t v = *((uint32_t *)(iter->this_arg));

	return le32toh(v);
}

static uint64_t
r_uint64(struct ieee80211_radiotap_iterator *iter)
{
	uint64_t v = *((uint64_t *)(iter->this_arg));

	return le64toh(v);
}

static int8_t
r_int8(struct ieee80211_radiotap_iterator *iter)
{
	return (int8_t)r_uint8(iter);
}

static int16_t __attribute__((unused))
r_int16(struct ieee80211_radiotap_iterator *iter)
{
	return (int16_t)r_uint16(iter);
}

static int32_t __attribute__((unused))
r_int32(struct ieee80211_radiotap_iterator *iter)
{
	return (int32_t)r_uint32(iter);
}

static int64_t __attribute__((unused))
r_int64(struct ieee80211_radiotap_iterator *iter)
{
	return (int64_t)r_uint64(iter);
}

static struct radiotap_channel
r_channel(struct ieee80211_radiotap_iterator *iter)
{
	struct radiotap_channel ch;

	ch.freq = le16toh(*((uint16_t *)iter->this_arg));
	ch.flags = le16toh(*((uint16_t *)(iter->this_arg + 2)));

	return ch;
}

static struct radiotap_mcs
r_mcs(struct ieee80211_radiotap_iterator *iter)
{
	struct radiotap_mcs mcs;

	mcs.known = *(iter->this_arg);
	mcs.flags = *(iter->this_arg + 1);
	mcs.index = *(iter->this_arg + 2);

	return mcs;
}

void
radiotap_context_dump(const struct radiotap_context *ctx)
{
	if (ctx == NULL) {
		p_err("No context passed.\n");
		return;
	}

	p_info("TSFT: %llu\n", ctx->tsft);
	p_info("FLAGS: 0x%02x\n", ctx->flags);
	p_info("CHANNEL %d MHz, Flags 0x%04x\n",
	    ctx->channel.freq, ctx->channel.flags);
	p_info("DBM_SIGNAL: %d\n", ctx->dbm_signal);
	p_info("ANTENNA: %u\n", ctx->antenna);
	p_info("RX_FLAGS: 0x%04x\n", ctx->rx_flags);
	p_info("MCS: 0x%02x, 0x%02x, %d\n",
	    ctx->mcs.known, ctx->mcs.flags, ctx->mcs.index);
}

ssize_t
radiotap_frame_parse(void *data, size_t size, struct radiotap_context *ctx)
{
	struct ieee80211_radiotap_iterator iter;
	int err;

	if (data == NULL || size < sizeof(struct ieee80211_radiotap_header)) {
		p_err("Frame too short\n");
		return 0;
	}

	err = ieee80211_radiotap_iterator_init(&iter, data, size, NULL);
	if (err < 0) {
		p_err("malformed radiotap header\n");
		return -1;
	}

	while (!(err = ieee80211_radiotap_iterator_next(&iter))) {
		if (!iter.is_radiotap_ns) {
			/* ignore unknown name spaces */
			continue;
		}
		switch (iter.this_arg_index) {
			case IEEE80211_RADIOTAP_TSFT:
				if (ctx)
					ctx->tsft = r_uint64(&iter);
				break;
			case IEEE80211_RADIOTAP_FLAGS:
				if (ctx)
					ctx->flags = r_uint8(&iter);
				break;
			case IEEE80211_RADIOTAP_CHANNEL:
				if (ctx)
					ctx->channel = r_channel(&iter);
				break;
			case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
				if (ctx)
					ctx->dbm_signal = r_int8(&iter);
				break;
			case IEEE80211_RADIOTAP_ANTENNA:
				if (ctx)
					ctx->antenna = r_uint8(&iter);
				break;
			case IEEE80211_RADIOTAP_RX_FLAGS:
				if (ctx)
					ctx->rx_flags = r_uint16(&iter);
				break;
			case IEEE80211_RADIOTAP_MCS:
				if (ctx)
					ctx->mcs = r_mcs(&iter);
				break;
			case IEEE80211_RADIOTAP_RATE:
			case IEEE80211_RADIOTAP_FHSS:
			case IEEE80211_RADIOTAP_DBM_ANTNOISE:
			case IEEE80211_RADIOTAP_LOCK_QUALITY:
			case IEEE80211_RADIOTAP_TX_ATTENUATION:
			case IEEE80211_RADIOTAP_DBM_TX_POWER:
			case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
			case IEEE80211_RADIOTAP_DB_ANTNOISE:
			case IEEE80211_RADIOTAP_TX_FLAGS:
			case IEEE80211_RADIOTAP_RTS_RETRIES:
			case IEEE80211_RADIOTAP_DATA_RETRIES:
			case IEEE80211_RADIOTAP_AMPDU_STATUS:
			case IEEE80211_RADIOTAP_VHT:
			case IEEE80211_RADIOTAP_TIMESTAMP:
			default:
				/* Ignore */
				break;
		}
	}
	if (err != -ENOENT) {
		p_err("malformed radiotap header\n");
		return -1;
	}

	ctx->has_fcs = (ctx->flags & IEEE80211_RADIOTAP_F_FCS) ? true : false;
	ctx->bad_fcs =
	    (ctx->flags & IEEE80211_RADIOTAP_F_BADFCS) ? true : false;

	return iter._max_length;
}
