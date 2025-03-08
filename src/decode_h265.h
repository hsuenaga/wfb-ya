#ifndef __DECODE_H265_H__
#define __DECODE_H265_H__
#include <stdint.h>
#include <pthread.h>

#include <gst/gst.h>

struct decode_h265_context {
	GstElement *pipeline;
	GstElement *input_queue;
	GstElement *source;
	GstElement *count1;
	GstElement *rtp;
	GstElement *count2;
	GstElement *h265_parse;
	GstElement *h265;
	GstElement *conv;
	GstElement *sink_queue;
	GstElement *count3;
	GstElement *sink;
	GMainLoop *loop;

	pthread_t tid_loop;
	int bus_watch_id;
	int closing;
};

extern int decode_h265_context_init(struct decode_h265_context *ctx);
extern void decode_h265_context_deinit(struct decode_h265_context *ctx);
extern void decode_h265(uint8_t *data, size_t size, void *arg);
#endif /* __DECODE_H265_H__ */
