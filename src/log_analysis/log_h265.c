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
	wfb_gst_context_init(ctx, NULL, false);
	wfb_gst_thread_start(ctx);
}

void
play_h265(struct log_store *ls)
{
	struct timespec epoch, elapsed;
	struct log_data_kv *kv;
	struct log_data_v *v;

	init_player(&player_ctx);

	clock_gettime(CLOCK_MONOTONIC, &epoch);
	timespecclear(&elapsed);
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->type != FRAME_TYPE_DECODE)
				continue;
			if (v->filtered)
				continue;
			for (;;) {
				clock_gettime(CLOCK_MONOTONIC, &elapsed);
				timespecsub(&elapsed, &epoch, &elapsed);
				if (timespeccmp(&elapsed, &v->ts, <)) {
					usleep(1);
					continue;
				}
				wfb_gst_write(&v->ts,
				    v->buf, v->size, &player_ctx);
				break;
			}
		}
	}
	wfb_gst_eos(&player_ctx);
}

void
write_mp4(const char *file, struct log_store *ls)
{
	struct wfb_gst_context ctx;
	struct timespec epoch, elapsed;
	struct log_data_kv *kv;
	struct log_data_v *v;

	if (file == NULL) {
		p_err("Please specify output file name.\n");
		exit(0);
	}

	wfb_gst_context_init(&ctx, file, false);
	wfb_gst_thread_start(&ctx);

	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->type != FRAME_TYPE_DECODE)
				continue;
			if (v->filtered)
				continue;
			wfb_gst_write(&v->ts, v->buf, v->size, &ctx);
		}
	}
	wfb_gst_eos(&ctx);
}

void
write_mp4_enc(const char *file, struct log_store *ls)
{
	struct wfb_gst_context ctx;
	struct timespec epoch, elapsed;
	struct log_data_kv *kv;
	struct log_data_v *v;

	if (file == NULL) {
		p_err("Please specify output file name.\n");
		exit(0);
	}

	wfb_gst_context_init(&ctx, file, true);
	wfb_gst_thread_start(&ctx);

	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->type != FRAME_TYPE_DECODE)
				continue;
			if (v->filtered)
				continue;
			wfb_gst_write(&v->ts, v->buf, v->size, &ctx);
		}
	}
	wfb_gst_eos(&ctx);
}
