#ifndef __WFB_GST_H__
#define __WFB_GST_H__
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#include <gst/gst.h>

struct wfb_gst_context {
	GMainLoop *loop;

	pthread_mutex_t lock;
	pthread_cond_t eos;
	pthread_t tid_loop;

	int bus_watch_id;
	int initialized;
	int closing;

	GstElement *pipeline;

	GstElement *source;
	GstElement *input_queue;
	GstElement *jitter;
	GstElement *rtp;
	GstElement *h265;
	GstElement *conv;
	GstElement *sink_queue;
	GstElement *mux;
	GstElement *sink;
};

extern int wfb_gst_context_init(struct wfb_gst_context *ctx, const char *file);
extern void wfb_gst_context_deinit(struct wfb_gst_context *ctx);
extern int wfb_gst_thread_start(struct wfb_gst_context *ctx);
extern int wfb_gst_thread_join(struct wfb_gst_context *ctx);

/* opaque argument 'void *arg' must be wfb_gst_context */
extern void wfb_gst_write(struct timespec *ts, uint8_t *data, size_t size, void *arg);
extern void wfb_gst_eos(void *arg);
#endif /* __WFB_GST_H__ */
