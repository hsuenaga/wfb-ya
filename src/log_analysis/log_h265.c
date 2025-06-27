#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/queue.h>
#include <sys/time.h>

#include "../rx_log.h"
#include "../compat.h"
#include "../util_msg.h"
#include "../wfb_gst.h"

#include "log_raw.h"
#include "log_h265.h"

static struct wfb_gst_context player_ctx = {
	.initialized = 0
};

static void
init_player(struct wfb_gst_context *ctx)
{
	assert(ctx);

	if (ctx->initialized)
		return;
	wfb_gst_context_init_play(ctx);
	wfb_gst_thread_start(ctx);
}

static void
init_writer(struct wfb_gst_context *ctx, const char *file, bool enc)
{
	assert(ctx);

	if (ctx->initialized)
		return;
	wfb_gst_context_init(ctx, file, enc, false);
	wfb_gst_thread_start(ctx);
}

static void
send_data(struct wfb_gst_context *ctx, struct log_store *ls)
{
	struct timespec epoch, elapsed;
	struct log_data_kv *kv;
	struct log_data_v *v;
	bool emulate_tick = true;

	assert(ctx);
	assert(ls);

	if (ctx->file && !ctx->enc)
		emulate_tick = false;

	if (emulate_tick) {
		clock_gettime(CLOCK_MONOTONIC, &epoch);
		timespecclear(&elapsed);
	}
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		int8_t dbm = INT8_MIN;
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->type == FRAME_TYPE_INET6) {
				if (v->dbm > dbm) 
					dbm = v->dbm;
				continue;
			}
			if (v->type != FRAME_TYPE_DECODE)
				continue;
			if (v->filtered)
				continue;
			for (;;) {
				if (emulate_tick) {
					clock_gettime(CLOCK_MONOTONIC,
					    &elapsed);
					timespecsub(&elapsed, &epoch,
					    &elapsed);
					if (timespeccmp(&elapsed, &v->ts, <)) {
						usleep(1);
						continue;
					}
				}
				wfb_gst_add_dbm(&player_ctx, dbm);
				wfb_gst_write(&player_ctx,
				    &v->ts, v->buf, v->size);
				break;
			}
		}
	}
	wfb_gst_eos(&player_ctx);
}

void
play_h265(struct log_store *ls)
{
	assert(ls);

	init_player(&player_ctx);
	send_data(&player_ctx, ls);
}

void
write_mp4(const char *file, struct log_store *ls)
{
	struct wfb_gst_context ctx;

	assert(ls);

	if (file == NULL) {
		p_err("Please specify output file name.\n");
		exit(0);
	}

	memset(&ctx, 0, sizeof(ctx));
	init_writer(&ctx, file, false);
	send_data(&ctx, ls);
}

void
write_mp4_enc(const char *file, struct log_store *ls)
{
	struct wfb_gst_context ctx;

	assert(ls);

	if (file == NULL) {
		p_err("Please specify output file name.\n");
		exit(0);
	}

	memset(&ctx, 0, sizeof(ctx));
	init_writer(&ctx, file, true);
	send_data(&ctx, ls);
}
