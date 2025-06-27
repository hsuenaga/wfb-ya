#ifndef __WFB_GST_H__
#define __WFB_GST_H__
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>

#include <gst/gst.h>

#define	OVERLAY_NHIST 300

struct wfb_gst_context {
	GMainLoop *loop;

	pthread_mutex_t lock;
	pthread_cond_t eos;
	pthread_t tid_loop;

	int bus_watch_id;
	bool initialized;
	bool closing;
	bool eos_detected;
	bool enc;
	const char *file;

	GstElement *pipeline;

	GstElement *source;		/* BIN */
	GstElement *appsrc;		/* not BIN */
	GstElement *rtp;		/* BIN */
	GstElement *codec;		/* BIN */
	GstElement *overlay;		/* BIN */
	GstElement *sink;		/* BIN */

	/* Overlay data */
	int8_t history[OVERLAY_NHIST];
	int history_cur;
};

extern int wfb_gst_context_init(struct wfb_gst_context *ctx, const char *file,
    bool enc);
extern void wfb_gst_context_deinit(struct wfb_gst_context *ctx);
extern int wfb_gst_thread_start(struct wfb_gst_context *ctx);
extern int wfb_gst_thread_join(struct wfb_gst_context *ctx);

/* opaque argument 'void *arg' must be wfb_gst_context */
extern void wfb_gst_add_dbm(int8_t dbm, void *arg);
extern void wfb_gst_write(struct timespec *ts, uint8_t *data, size_t size, void *arg);
extern void wfb_gst_eos(void *arg);

extern void wfb_gst_handler(int8_t rssi, uint8_t *data, size_t size, void *arg);
#endif /* __WFB_GST_H__ */
