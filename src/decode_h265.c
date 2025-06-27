#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <gst/gst.h>

#include "decode_h265.h"
#include "util_msg.h"

static void *
thread_main(void *arg)
{
	struct decode_h265_context *ctx = arg; 
	GMainLoop *loop;

	/* decoder thread */

	pthread_mutex_lock(&ctx->lock);
	ctx->loop = loop = g_main_loop_new(NULL, FALSE);
	pthread_mutex_unlock(&ctx->lock);

	g_main_loop_run(loop);

	pthread_mutex_lock(&ctx->lock);
	ctx->loop = NULL;
	ctx->closing = 1;
	pthread_mutex_unlock(&ctx->lock);

	g_main_loop_unref(loop);

	return NULL;
}

static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer arg)
{
	struct decode_h265_context *ctx = arg;
	GError *err;
	gchar *debug_info;

	/* called from decoder thread */

	switch (GST_MESSAGE_TYPE(msg)) {
		case GST_MESSAGE_ERROR:
			gst_message_parse_error(msg, &err, &debug_info);
			p_err("GST Error %s: %s\n",
			    GST_OBJECT_NAME(msg->src),
			    err->message);
			p_err("Debug: %s\n",
			    debug_info ? debug_info : "none");
			g_clear_error(&err);
			g_free(debug_info);

			pthread_mutex_lock(&ctx->lock);
			if (!ctx->closing) {
				ctx->closing = 1;
				g_main_loop_quit(ctx->loop);
			}
			pthread_mutex_unlock(&ctx->lock);
			break;
		case GST_MESSAGE_WARNING:
			gst_message_parse_warning(msg, &err, &debug_info);
			p_err("GST Warning %s: %s\n",
			    GST_OBJECT_NAME(msg->src),
			    err->message);
			p_err("Debug: %s\n",
			    debug_info ? debug_info : "none");
			g_clear_error(&err);
			g_free(debug_info);
			break;
		case GST_MESSAGE_EOS:
			p_info("End-Of-Stream reached.\n");
			break;
		case GST_MESSAGE_STATE_CHANGED:
		case GST_MESSAGE_STREAM_STATUS:
		case GST_MESSAGE_STREAM_START:
		case GST_MESSAGE_NEW_CLOCK:
		case GST_MESSAGE_ELEMENT:
		case GST_MESSAGE_TAG:
		case GST_MESSAGE_QOS:
		case GST_MESSAGE_LATENCY:
		case GST_MESSAGE_ASYNC_DONE:
		case GST_MESSAGE_NEED_CONTEXT:
		case GST_MESSAGE_HAVE_CONTEXT:
			break;
		default:
			p_err("Unhandled message %s => %s\n",
			   GST_OBJECT_NAME(msg->src),
			   GST_MESSAGE_TYPE_NAME(msg));
			break;
	}

	return TRUE;
}

int
decode_h265_context_init(struct decode_h265_context *ctx)
{
	GstStateChangeReturn ret;
	GstCaps *caps;
	GstBus *bus;

	assert(ctx);
	memset(ctx, 0, sizeof(*ctx));

	pthread_mutex_init(&ctx->lock, NULL);

	p_debug("Initializing gst\n");
	gst_init(NULL, NULL);

	p_debug("setup gst\n");
	ctx->input_queue = gst_element_factory_make("queue", "input_queue");
	ctx->source = gst_element_factory_make("appsrc", "source");
	ctx->rtp = gst_element_factory_make("rtph265depay", "rtp");
	ctx->h265 = gst_element_factory_make("v4l2slh265dec", "h265");
	if (!ctx->h265)
		ctx->h265 = gst_element_factory_make("avdec_h265", "h265");
	if (!ctx->h265)
		ctx->h265 = gst_element_factory_make("libde265dec", "h265");
	ctx->conv = gst_element_factory_make("videoconvert", "conv");
	ctx->sink_queue = gst_element_factory_make("queue", "sink_queue");
	ctx->sink = gst_element_factory_make("autovideosink", "sink");

	ctx->pipeline = gst_pipeline_new("main-pipeline");

	gst_bin_add_many(GST_BIN(ctx->pipeline),
	    ctx->source,
	    ctx->input_queue,
	    ctx->rtp,
	    ctx->h265,
	    ctx->conv,
	    ctx->sink_queue,
	    ctx->sink,
	    NULL);
	gst_element_link_many(
	    ctx->source,
	    ctx->input_queue,
	    ctx->rtp,
	    ctx->h265,
	    ctx->conv,
	    ctx->sink_queue,
	    ctx->sink,
	    NULL);

	gst_util_set_object_arg (G_OBJECT (ctx->source), "format", "time");
	caps = gst_caps_new_simple("application/x-rtp",
			"media", G_TYPE_STRING, "video",
			"clock-rate", G_TYPE_INT, 90000,
			"encoding-name", G_TYPE_STRING, "H265",
			"framerate", G_TYPE_INT, 120,
			NULL);

	g_object_set(ctx->source, "caps", caps, NULL);
	g_object_set(ctx->source, "block", FALSE, NULL);
	g_object_set(ctx->source, "emit-signals", FALSE, NULL);
	g_object_set(ctx->source, "is-live", TRUE, NULL);
	gst_caps_unref(caps);

	g_object_set(ctx->sink, "sync", FALSE, NULL);

	p_debug("start playing\n");
	ret = gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		p_err("Unable to set the pipeline to the playing state.\n");
		gst_object_unref(ctx->pipeline);
		return -1;
	}

	bus = gst_element_get_bus(ctx->pipeline);
	assert(bus);
	ctx->bus_watch_id = gst_bus_add_watch(bus, bus_call, ctx);
	gst_object_unref(bus);

	ctx->initialized = 1;

	return 0;
}

void
decode_h265_context_deinit(struct decode_h265_context *ctx)
{
	GstStateChangeReturn r;
	void *ret;

	/* called from main thread */

	if (!ctx->initialized)
		return;

	pthread_mutex_lock(&ctx->lock);

	if (!ctx->closing) {
		r = gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
		if (r == GST_STATE_CHANGE_ASYNC) {
			gst_element_get_state(ctx->pipeline,
			    NULL, NULL,  GST_CLOCK_TIME_NONE);
		}
		ctx->closing = 1;
		g_main_loop_quit(ctx->loop);
	}

	pthread_mutex_unlock(&ctx->lock);

	pthread_join(ctx->tid, &ret);

	DECODE_H265_UNREF_OBJ(input_queue, false);
	DECODE_H265_UNREF_OBJ(source, false);
	DECODE_H265_UNREF_OBJ(rtp, false);
	DECODE_H265_UNREF_OBJ(h265, false);
	DECODE_H265_UNREF_OBJ(conv, false);
	DECODE_H265_UNREF_OBJ(sink_queue, false);
	DECODE_H265_UNREF_OBJ(sink, false);
	DECODE_H265_UNREF_OBJ(loop, false);
	DECODE_H265_UNREF_OBJ(pipeline, true);
	g_source_remove(ctx->bus_watch_id);
	ctx->initialized = 0;
}

int
decode_h265_thread_start(struct decode_h265_context *ctx)
{
	/* called from main thread */

	assert(ctx);

	if (!ctx->initialized) {
		p_debug("Decoder context is not initialized.\n");
		return -1;
	}
	pthread_create(&ctx->tid, NULL, thread_main, ctx);

	return 0;
}

int
decode_h265_thread_join(struct decode_h265_context *ctx)
{
	/* called from main thread */
	assert(ctx);

	decode_h265_context_deinit(ctx);

	return 0;
}

void
decode_h265(int8_t rssi, uint8_t *data, size_t size, void *arg)
{
	struct decode_h265_context *ctx = arg;
	GstBuffer *buf;
	int ret;

	/* called from main thread */

	assert(ctx);
	assert(data);
	assert(size);

	pthread_mutex_lock(&ctx->lock);

	if (!ctx->initialized) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}
	if (ctx->closing) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}

	buf = gst_buffer_new_memdup(data, size);
	assert(buf);

	g_signal_emit_by_name(ctx->source, "push-buffer", buf, &ret);
	gst_buffer_unref(buf);

	pthread_mutex_unlock(&ctx->lock);

	return;
}
