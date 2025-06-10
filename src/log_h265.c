#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/queue.h>
#include <sys/time.h>

#include "compat.h"
#include "util_log.h"

#include "log_raw.h"
#include "log_h265.h"
#include "wfb_gst.h"

void
play_h265(struct log_store *ls)
{
	struct wfb_gst_context ctx;
	struct timespec epoch, elapsed;
	struct log_data_kv *kv;
	struct log_data_v *v;

	wfb_gst_context_init(&ctx, NULL);
	wfb_gst_thread_start(&ctx);

	clock_gettime(CLOCK_MONOTONIC, &epoch);
	timespecclear(&elapsed);
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->size == 0)
				continue;
			wfb_gst(&v->ts, v->buf, v->size, &ctx);
		}
	}
	wfb_gst(NULL, NULL, 0, &ctx);
	wfb_gst_thread_join(&ctx);
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

	wfb_gst_context_init(&ctx, file);
	wfb_gst_thread_start(&ctx);

	clock_gettime(CLOCK_MONOTONIC, &epoch);
	timespecclear(&elapsed);
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->size == 0)
				continue;
			wfb_gst(&v->ts, v->buf, v->size, &ctx);
		}
	}
	wfb_gst(NULL, NULL, 0, &ctx);
	wfb_gst_thread_join(&ctx);
}
