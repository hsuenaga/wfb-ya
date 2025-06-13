#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>

#include "rx_core.h"
#include "rx_log.h"
#include "util_msg.h"

#include "compat.h"

static void
fixup_filename(struct rx_log_handler *log)
{
	struct stat st;
	int rc;

	assert(log);

	if (log->seq == 0) {
		strlcpy(log->file_name, wfb_options.log_file,
		    sizeof(log->file_name));
	}
	p_debug("try file name: %s\n", log->file_name);

	while ( (rc = stat(log->file_name, &st)) == 0) {
		p_debug("stat() => %d\n", rc);
		log->seq++;
		snprintf(log->file_name, sizeof(log->file_name),
		  "%s.%d", wfb_options.log_file, log->seq);
	}
	p_debug("use file name: %s\n", log->file_name);
}

void
rx_log_corrupt(struct rx_context *ctx)
{
	struct rx_log_frame_header hd;
	struct rx_log_handler *log = &ctx->log_handler;
	struct timespec ts;

	if (log->fp == NULL)
		return;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		p_err("clock_gettime() failed: %s\n", strerror(errno));
		return;
	}

	memset(&hd, 0, sizeof(hd));
	hd.tv_sec = htole64(ts.tv_sec);
	hd.tv_nsec = htole64(ts.tv_nsec);
	hd.type = FRAME_TYPE_CORRUPT;
	hd.freq = htole16(ctx->freq);
	hd.dbm = htole16(ctx->dbm);
	if (ctx->rx_src.sin6_family == AF_INET) {
		memcpy(hd.rx_src, &ctx->rx_src.sin6_addr, sizeof(hd.rx_src));
	}
	(void)fwrite(&hd, sizeof(hd), 1, log->fp);
	fflush(log->fp);
}

void
rx_log_frame(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, size_t size)
{
	struct rx_log_frame_header hd;
	struct rx_log_handler *log = &ctx->log_handler;
	struct timespec ts;

	if (log->fp == NULL)
		return;
	if (size == 0)
		return;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		p_err("clock_gettime() failed: %s\n", strerror(errno));
		return;
	}

	memset(&hd, 0, sizeof(hd));
	hd.tv_sec = htole64(ts.tv_sec);
	hd.tv_nsec = htole64(ts.tv_nsec);
	hd.seq = htole64(block_idx * ctx->fec_n + fragment_idx);
	hd.block_idx = htole64(block_idx);
	hd.size = htole32(size);
	hd.fragment_idx = fragment_idx;
	hd.freq = htole16(ctx->freq);
	hd.dbm = htole16(ctx->dbm);
	if (ctx->rx_src.sin6_family == AF_INET6) {
		hd.type = FRAME_TYPE_INET6;
		memcpy(hd.rx_src, &ctx->rx_src.sin6_addr, sizeof(hd.rx_src));
	}

	p_debug("SEQ %" PRIu64 ", BLK %" PRIu64 ", FRAG %u, SIZE %lu\n",
	    hd.seq, hd.block_idx, hd.fragment_idx, size);

	(void)fwrite(&hd, sizeof(hd), 1, log->fp);
	fflush(log->fp);
}

void
rx_log_decode(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, uint8_t *data, size_t size)
{
	struct rx_log_frame_header hd;
	struct rx_log_handler *log = &ctx->log_handler;
	struct timespec ts;

	if (log->fp == NULL)
		return;
	if (data == NULL)
		return;
	if (size == 0)
		return;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		p_err("clock_gettime() failed: %s\n", strerror(errno));
		return;
	}

	memset(&hd, 0, sizeof(hd));
	hd.tv_sec = htole64(ts.tv_sec);
	hd.tv_nsec = htole64(ts.tv_nsec);
	hd.seq = htole64(block_idx * ctx->fec_n + fragment_idx);
	hd.block_idx = htole64(block_idx);
	hd.size = htole32(size);
	hd.fragment_idx = fragment_idx;
	hd.freq = htole16(0);
	hd.dbm = htole16(DBM_INVAL);
	hd.type = FRAME_TYPE_DECODE;
	memset(hd.rx_src, 0, sizeof(hd.rx_src));

	p_debug("SEQ %" PRIu64 ", BLK %" PRIu64 ", FRAG %u, SIZE %lu\n",
	    hd.seq, hd.block_idx, hd.fragment_idx, size);

	(void)fwrite(&hd, sizeof(hd), 1, log->fp);
	(void)fwrite(data, size, 1, log->fp);
	fflush(log->fp);
}

void
rx_log_create(struct rx_context *ctx)
{
	struct rx_log_handler *log = &ctx->log_handler;
	struct rx_log_file_header hd;

	if (log->fp)
		fclose(log->fp);
	if (wfb_options.log_file == NULL) {
		log->fp = NULL;
		return;
	}
	fixup_filename(log);
	log->fp = fopen(log->file_name, "w");
	if (log->fp) {
		p_debug("New LOG File: %s\n", log->file_name);
	}
	else {
		p_err("Failed to create File %s: %s\n",
		    log->file_name, strerror(errno));
		return;
	}

	memset(&hd, 0, sizeof(hd));
	hd.version = RX_LOG_VERSION;
	hd.fec_type = ctx->fec_type;
	hd.fec_k = ctx->fec_k;
	hd.fec_n = ctx->fec_n;
	hd.channel_id = htole32(ctx->ieee80211.channel_id);
	hd.signature = htole32(RX_LOG_SIGNATURE);
	if (fwrite(&hd, sizeof(hd), 1, log->fp) == 0) {
		p_err("write failed: %s\n", strerror(errno));
		fclose(log->fp);
		log->fp = NULL;
	}
	fflush(log->fp);
}
