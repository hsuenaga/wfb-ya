#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <gst/gst.h>

#include "decode_h265.h"
#include "util_log.h"

static void
frame_count_callback(GstElement *elem, GstPad *pad, gpointer arg)
{
	struct decode_h265_context *ctx = arg;

	if (elem == ctx->count1) {
		fprintf(stderr, "1");
	}
	else if (elem == ctx->count2) {
		fprintf(stderr, "2");
	}
	else if (elem == ctx->count3) {
		fprintf(stderr, "3");
	}
	else {
		fprintf(stderr, "?");
	}
}

static void *
loop(void *arg)
{
	struct decode_h265_context *ctx = arg; 

	ctx->loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(ctx->loop);
	g_main_loop_unref(ctx->loop);

	return NULL;
}

static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer arg)
{
	struct decode_h265_context *ctx = arg;
	GError *err;
	gchar *debug_info;

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

			ctx->closing = 1;
			g_main_loop_quit(ctx->loop);
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
			break;
		default:
			p_err("Unhandled message %s\n",
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

	gst_init(NULL, NULL);

	ctx->input_queue = gst_element_factory_make("queue", "input_queue");
	ctx->source = gst_element_factory_make("appsrc", "source");
	ctx->rtp = gst_element_factory_make("rtph265depay", "rtp");
	ctx->h265_parse = gst_element_factory_make("h265parse", "h265_parse");
	ctx->h265 = gst_element_factory_make("v4l2slh265dec", "h265");
	if (!ctx->h265)
		ctx->h265 = gst_element_factory_make("avdec_h265", "h265");
	if (!ctx->h265)
		ctx->h265 = gst_element_factory_make("libde265dec", "h265");
	ctx->conv = gst_element_factory_make("videoconvert", "conv");
	ctx->sink_queue = gst_element_factory_make("queue", "sink_queue");
//	ctx->sink = gst_element_factory_make("autovideosink", "sink");
	ctx->sink = gst_element_factory_make("fakesink", "sink");

	ctx->pipeline = gst_pipeline_new("main-pipeline");

	ctx->count1 = gst_element_factory_make("identity", "count1");
	g_signal_connect(G_OBJECT(ctx->count1), "handoff",
	   G_CALLBACK(frame_count_callback), ctx);
	ctx->count2 = gst_element_factory_make("identity", "count2");
	g_signal_connect(G_OBJECT(ctx->count2), "handoff",
	   G_CALLBACK(frame_count_callback), ctx);
	ctx->count3 = gst_element_factory_make("identity", "count3");
	g_signal_connect(G_OBJECT(ctx->count3), "handoff",
	   G_CALLBACK(frame_count_callback), ctx);

	gst_bin_add_many(GST_BIN(ctx->pipeline),
	    ctx->source,
	    ctx->input_queue,
	    ctx->count1,
	    ctx->rtp,
	    ctx->count2,
	    ctx->h265_parse,
	    ctx->h265,
	    ctx->conv,
	    ctx->sink_queue,
	    ctx->count3,
	    ctx->sink,
	    NULL);
	gst_element_link_many(
	    ctx->source,
	    ctx->input_queue,
//	    ctx->count1,
	    ctx->rtp,
//	    ctx->count2,
	    ctx->h265_parse,
	    ctx->h265,
	    ctx->conv,
	    ctx->sink_queue,
//	    ctx->count3,
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

	pthread_create(&ctx->tid_loop, NULL, loop, ctx);

	return 0;
}

void
decode_h265_context_deinit(struct decode_h265_context *ctx)
{
	void *ret;

	ctx->closing = 1;
	gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
	g_main_loop_quit(ctx->loop);
	gst_object_unref(ctx->pipeline);
	pthread_join(ctx->tid_loop, &ret);

	memset(ctx, 0, sizeof(*ctx));
}

void
decode_h265(uint8_t *data, size_t size, void *arg)
{
	struct decode_h265_context *ctx = arg;
	GstBuffer *buf;
	int ret;

	assert(ctx);
	assert(data);
	assert(size);

	buf = gst_buffer_new_memdup(data, size);
	assert(buf);

	g_signal_emit_by_name(ctx->source, "push-buffer", buf, &ret);
	gst_buffer_unref(buf);
	fprintf(stderr, ".");

	return;
}

