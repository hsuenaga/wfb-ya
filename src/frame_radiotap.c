#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <radiotap.h>
#include <radiotap_iter.h>

#include "compat.h"

#include "frame_radiotap.h"
#include "util_log.h"

void
radiotap_tx_context_initialize(struct radiotap_tx_context *ctx,
    struct radiotap_params *param)
{
	struct ieee80211_radiotap_header *hdr;
	uint8_t *payload;

	assert(ctx);
	memset(ctx, 0, sizeof(*ctx));

#define EMBED_ARG(bit, arg) do { \
	if (param->arg) { \
		assert((hdr->it_len + sizeof(*param->arg)) \
		    < sizeof(ctx->tx_data)); \
		hdr->it_present |= (1 << bit); \
		memcpy(payload, param->arg, sizeof(*param->arg)); \
		payload += sizeof(*param->arg); \
		hdr->it_len += sizeof(*param->arg); \
	} \
} while (0)

	hdr = (struct ieee80211_radiotap_header *)ctx->tx_data;
	payload = (uint8_t *)(hdr + 1);
	
	hdr->it_version = 0;
	hdr->it_pad = 0;
	hdr->it_len = sizeof(*hdr);
	hdr->it_present = 0;

	EMBED_ARG(IEEE80211_RADIOTAP_TSFT, tsft);
	EMBED_ARG(IEEE80211_RADIOTAP_FLAGS, flags);
	EMBED_ARG(IEEE80211_RADIOTAP_RATE, rate);
	EMBED_ARG(IEEE80211_RADIOTAP_CHANNEL, channel);
	EMBED_ARG(IEEE80211_RADIOTAP_FHSS, fhss);
	EMBED_ARG(IEEE80211_RADIOTAP_DBM_ANTSIGNAL, dbm_antenna_signal);
	EMBED_ARG(IEEE80211_RADIOTAP_DBM_ANTNOISE, dbm_antenna_noise);
	EMBED_ARG(IEEE80211_RADIOTAP_LOCK_QUALITY, lock_quality);
	EMBED_ARG(IEEE80211_RADIOTAP_TX_ATTENUATION, tx_att);
	EMBED_ARG(IEEE80211_RADIOTAP_DB_TX_ATTENUATION, db_tx_att);
	EMBED_ARG(IEEE80211_RADIOTAP_DBM_TX_POWER, tx_power);
	EMBED_ARG(IEEE80211_RADIOTAP_ANTENNA, antenna);
	EMBED_ARG(IEEE80211_RADIOTAP_DB_ANTSIGNAL, db_antenna_signal);
	EMBED_ARG(IEEE80211_RADIOTAP_DB_ANTNOISE, db_antenna_noise);
	EMBED_ARG(IEEE80211_RADIOTAP_RX_FLAGS, rx_flags);
	EMBED_ARG(IEEE80211_RADIOTAP_TX_FLAGS, tx_flags);
	EMBED_ARG(IEEE80211_RADIOTAP_RTS_RETRIES, rts_retries);
	EMBED_ARG(IEEE80211_RADIOTAP_DATA_RETRIES, data_retries);
	EMBED_ARG(IEEE80211_RADIOTAP_MCS, mcs);
	EMBED_ARG(IEEE80211_RADIOTAP_AMPDU_STATUS, ampdu);
	EMBED_ARG(IEEE80211_RADIOTAP_VHT, vht);
	EMBED_ARG(IEEE80211_RADIOTAP_TIMESTAMP, timestamp);

	ctx->tx_iov.iov_base = (char *)hdr;
	ctx->tx_iov.iov_len = hdr->it_len;

	hdr->it_len = htole16(hdr->it_len);
	hdr->it_present = htole32(hdr->it_present);
}

void
radiotap_context_dump(const struct radiotap_context *ctx)
{
	assert(ctx);

	if (ctx == NULL) {
		p_err("No context passed.\n");
		return;
	}

	p_info("TSFT: %llu\n", le64toh(*ctx->raw.tsft));
	p_info("FLAGS: 0x%02x\n", *ctx->raw.flags);
	p_info("CHANNEL %d MHz, Flags 0x%04x\n",
	    le16toh(ctx->raw.channel->freq),
	    le16toh(ctx->raw.channel->flags));
	p_info("DBM_SIGNAL: %d\n", *ctx->raw.dbm_antenna_signal);
	p_info("ANTENNA: %u\n", *ctx->raw.antenna);
	p_info("RX_FLAGS: 0x%04x\n", le16toh(*ctx->raw.rx_flags));
	p_info("MCS: 0x%02x, 0x%02x, %d\n",
	    ctx->raw.mcs->known, ctx->raw.mcs->flags, ctx->raw.mcs->index);
}

ssize_t
radiotap_frame_parse(void *data, size_t size, struct radiotap_context *ctx)
{
	struct ieee80211_radiotap_iterator iter;
	int err;

	assert(ctx);
	assert(data);

	memset(ctx, 0, sizeof(*ctx));

	if (size < sizeof(struct ieee80211_radiotap_header)) {
		p_err("Frame too short\n");
		return 0;
	}

	err = ieee80211_radiotap_iterator_init(&iter, data, size, NULL);
	if (err < 0) {
		p_err("malformed radiotap header\n");
		return -1;
	}

	while (!(err = ieee80211_radiotap_iterator_next(&iter))) {
		void *arg = (void *)iter.this_arg;

		if (!iter.is_radiotap_ns) {
			/* ignore vendor name spaces */
			continue;
		}
		switch (iter.this_arg_index) {
			case IEEE80211_RADIOTAP_TSFT:
				ctx->raw.tsft = arg;
				break;
			case IEEE80211_RADIOTAP_FLAGS:
				ctx->raw.flags = arg;
				break;
			case IEEE80211_RADIOTAP_RATE:
				ctx->raw.rate = arg;
				break;
			case IEEE80211_RADIOTAP_CHANNEL:
				ctx->raw.channel = arg;
				break;
			case IEEE80211_RADIOTAP_FHSS:
				ctx->raw.fhss = arg;
				break;
			case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
				ctx->raw.dbm_antenna_signal = arg;
				break;
			case IEEE80211_RADIOTAP_DBM_ANTNOISE:
				ctx->raw.dbm_antenna_noise = arg;
				break;
			case IEEE80211_RADIOTAP_LOCK_QUALITY:
				ctx->raw.lock_quality = arg;
				break;
			case IEEE80211_RADIOTAP_TX_ATTENUATION:
				ctx->raw.tx_att = arg;
				break;
			case IEEE80211_RADIOTAP_DB_TX_ATTENUATION:
				ctx->raw.db_tx_att = arg;
				break;
			case IEEE80211_RADIOTAP_DBM_TX_POWER:
				ctx->raw.tx_power = arg;
				break;
			case IEEE80211_RADIOTAP_ANTENNA:
				ctx->raw.antenna = arg;
				break;
			case IEEE80211_RADIOTAP_DB_ANTSIGNAL:
				ctx->raw.db_antenna_signal = arg;
				break;
			case IEEE80211_RADIOTAP_DB_ANTNOISE:
				ctx->raw.db_antenna_noise = arg;
				break;
			case IEEE80211_RADIOTAP_RX_FLAGS:
				ctx->raw.rx_flags = arg;
				break;
			case IEEE80211_RADIOTAP_TX_FLAGS:
				ctx->raw.tx_flags = arg;
				break;
			case IEEE80211_RADIOTAP_RTS_RETRIES:
				ctx->raw.rts_retries = arg;
				break;
			case IEEE80211_RADIOTAP_DATA_RETRIES:
				ctx->raw.data_retries = arg;
				break;
			case IEEE80211_RADIOTAP_MCS:
				ctx->raw.mcs = arg;
				break;
			case IEEE80211_RADIOTAP_AMPDU_STATUS:
				ctx->raw.ampdu = arg;
				break;
			case IEEE80211_RADIOTAP_VHT:
				ctx->raw.vht = arg;
				break;
			case IEEE80211_RADIOTAP_TIMESTAMP:
				ctx->raw.timestamp = arg;
				break;
			default:
				/*
				 * bit 18, 23 - 34 is described * in
				 * radiotap.org, but not defined in their
				 * library.
				 */
				break;
		}
	}
	if (err != -ENOENT) {
		p_err("malformed radiotap header\n");
		return -1;
	}

	ctx->has_fcs =
	    (*ctx->raw.flags & IEEE80211_RADIOTAP_F_FCS) ? true : false;
	ctx->bad_fcs =
	    (*ctx->raw.flags & IEEE80211_RADIOTAP_F_BADFCS) ? true : false;
	if (ctx->raw.channel) {
		ctx->freq = le16toh(ctx->raw.channel->freq);
		ctx->flags = le16toh(ctx->raw.channel->flags);
	}
	if (ctx->raw.dbm_antenna_signal) {
		ctx->dbm = *ctx->raw.dbm_antenna_signal;
	}
	else {
		ctx->dbm = INT16_MIN;
	}

	return iter._max_length;
}
