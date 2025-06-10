#ifndef __WFB_GST_H__
#define __WFB_GST_H__
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>

#include <gst/gst.h>

struct wfb_gst_context {
	GstElement *pipeline;
	GstElement *input_queue;
	GstElement *source;
	GstElement *jitter;
	GstElement *rtp;
	GstElement *timestamp;
	GstElement *h265;
	GstElement *conv;
	GstElement *sink_queue;
	GstElement *mux;
	GstElement *sink;
	GstElement *timeoverlay;
	GMainLoop *loop;

	pthread_mutex_t lock;
	pthread_cond_t eos;
	pthread_t tid_loop;
	int bus_watch_id;
	int initialized;
	int closing;
};

extern int wfb_gst_context_init(struct wfb_gst_context *ctx, const char *file);
extern void wfb_gst_context_deinit(struct wfb_gst_context *ctx);
extern int wfb_gst_thread_start(struct wfb_gst_context *ctx);
extern int wfb_gst_thread_join(struct wfb_gst_context *ctx);
extern void wfb_gst(struct timespec *ts, uint8_t *data, size_t size, void *arg);
#endif /* __WFB_GST_H__ */
