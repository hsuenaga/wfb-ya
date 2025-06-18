#ifndef __DECODE_H265_H__
#define __DECODE_H265_H__
#include <stdint.h>
#include <pthread.h>

#include <gst/gst.h>

struct decode_h265_context {
	GstElement *pipeline;
	GstElement *input_queue;
	GstElement *source;
	GstElement *rtp;
	GstElement *h265;
	GstElement *conv;
	GstElement *sink_queue;
	GstElement *sink;
	GMainLoop *loop;

	pthread_mutex_t lock;
	pthread_t tid;
	int bus_watch_id;
	int initialized;
	int closing;
};

#define DECODE_H265_UNREF_OBJ(name, owner)			\
	do {							\
		if (ctx->name != NULL) {			\
			if (owner)				\
				gst_object_unref(ctx->name);	\
			ctx->name = NULL;			\
		}						\
	} while (/* CONSTCOND */ 0);

extern int decode_h265_context_init(struct decode_h265_context *ctx);
extern void decode_h265_context_deinit(struct decode_h265_context *ctx);
extern int decode_h265_thread_start(struct decode_h265_context *ctx);
extern int decode_h265_thread_join(struct decode_h265_context *ctx);
extern void decode_h265(uint8_t *data, size_t size, void *arg);
#endif /* __DECODE_H265_H__ */
