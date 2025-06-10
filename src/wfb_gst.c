#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include "wfb_gst.h"
#include "util_log.h"

static void
set_timestamp(GstBuffer *buf, struct timespec *ts)
{
	uint64_t nsec;

	nsec = ts->tv_sec * 1000 * 1000 * 1000;
	nsec += ts->tv_nsec;

	GST_BUFFER_TIMESTAMP(buf) = nsec;
	GST_BUFFER_PTS(buf) = nsec;
	GST_BUFFER_DTS(buf) = nsec;
	GST_BUFFER_DURATION(buf) = GST_CLOCK_TIME_NONE;
}

static void *
loop(void *arg)
{
	struct wfb_gst_context *ctx = arg; 

	ctx->loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(ctx->loop);
	g_main_loop_unref(ctx->loop);

	return NULL;
}

static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer arg)
{
	struct wfb_gst_context *ctx = arg;
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
			pthread_cond_signal(&ctx->eos);
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
wfb_gst_init_bus(struct wfb_gst_context *ctx)
{
	GstBus *bus;

	assert(ctx);

	bus = gst_element_get_bus(ctx->pipeline);
	if (bus == NULL)
		return -1;

	ctx->bus_watch_id = gst_bus_add_watch(bus, bus_call, ctx);
	gst_object_unref(bus);

	return 0;
}

int
wfb_gst_init_source(struct wfb_gst_context *ctx)
{
	GstCaps *caps;

	assert(ctx);

	ctx->source = gst_element_factory_make("appsrc", "source");
	if (ctx->source == NULL)
		return -1;

	gst_util_set_object_arg (G_OBJECT (ctx->source), "format", "time");
	caps = gst_caps_new_simple("application/x-rtp",
			"media", G_TYPE_STRING, "video",
			"clock-rate", G_TYPE_INT, 90000,
			"encoding-name", G_TYPE_STRING, "H265",
//			"framerate", G_TYPE_INT, 120,
//			"payload", G_TYPE_INT, 96,
			NULL);
	if (caps == NULL)
		return -1;

	g_object_set(ctx->source, "caps", caps, NULL);
	g_object_set(ctx->source, "block", TRUE, NULL);
	g_object_set(ctx->source, "emit-signals", FALSE, NULL);
	g_object_set(ctx->source, "is-live", TRUE, NULL);
	g_object_set(ctx->source, "do-timestamp", FALSE, NULL);
	gst_caps_unref(caps);

	return 0;
}

int
wfb_gst_init_codec(struct wfb_gst_context *ctx, const char *file)
{
	assert(ctx);

	if (file) {
		ctx->h265 = gst_element_factory_make("h265parse", "h265");
		if (!ctx->h265) {
			p_err("Cannot find H.265 parser.\n");
			return -1;
		}
		p_info("Using h265parse\n");
		return 0;
	}

	ctx->h265 = gst_element_factory_make("v4l2slh265dec", "h265");
	if (ctx->h265) {
		p_info("Uisng v4l2slh265dec\n");
		return 0;
	}
	else {
		ctx->h265 = gst_element_factory_make("avdec_h265", "h265");
	}
	if (ctx->h265) {
		p_info("Uisng avdec_h265\n");
		return 0;
	}
	else {
		ctx->h265 = gst_element_factory_make("libde265dec", "h265");
	}
	if (ctx->h265) {
		p_info("Uisng libde265dec\n");
		return 0;
	}

	p_err("Cannot find H.265 codec.\n");
	return -1;
}

int
wfb_gst_context_init(struct wfb_gst_context *ctx, const char *file)
{
	GstStateChangeReturn ret;

	assert(ctx);
	memset(ctx, 0, sizeof(*ctx));

	pthread_mutex_init(&ctx->lock, NULL);
	pthread_cond_init(&ctx->eos, NULL);

	p_debug("Initializing gst\n");
	gst_init(NULL, NULL);

	p_debug("setup gst\n");

	/* AppSrc: main thread */
	if (wfb_gst_init_source(ctx) < 0) {
		p_err("failed to initialize gst source element.\n");
		return -1;
	}

	/* Input queue: RTP and CODEC thread */
	ctx->input_queue = gst_element_factory_make("queue", "input_queue");

	ctx->jitter = gst_element_factory_make("rtpjitterbuffer", "jitter");
	g_object_set(ctx->jitter, "latency", 10, NULL); // [ms]
							//
	ctx->rtp = gst_element_factory_make("rtph265depay", "rtp");
	g_object_set(ctx->rtp, "auto-header-extension", TRUE, NULL);

	if (wfb_gst_init_codec(ctx, file) < 0) {
		p_err("failed to initialize gst codec element.\n");
		return -1;
	}

	/* Sink queue: writing file or playing movie thread */
	ctx->sink_queue = gst_element_factory_make("queue", "sink_queue");

	if (file) {
		ctx->conv = gst_element_factory_make("h265timestamper", "conv");
		ctx->mux = gst_element_factory_make("qtmux", "mux");
		ctx->sink = gst_element_factory_make("filesink", "sink");
		g_object_set(ctx->sink, "location", file, NULL);
	}
	else {
		ctx->conv = gst_element_factory_make("videoconvert", "conv");
		ctx->mux = gst_element_factory_make("timeoverlay", "mux");
		ctx->sink = gst_element_factory_make("autovideosink", "sink");
		g_object_set(ctx->sink, "sync", TRUE, NULL);
	}

	/* Create pipeline with initialized elements above */
	ctx->pipeline = gst_pipeline_new("main-pipeline");

	p_debug("add elements\n");
	gst_bin_add_many(GST_BIN(ctx->pipeline),
	    ctx->source,
	    ctx->input_queue,
	    ctx->jitter,
	    ctx->rtp,
	    ctx->h265,
	    ctx->conv,
	    ctx->sink_queue,
	    ctx->mux,
	    ctx->sink,
	    NULL);

	p_debug("link elements\n");
	gst_element_link_many(
	    ctx->source,
	    ctx->input_queue,
	    ctx->jitter,
	    ctx->rtp,
	    ctx->h265,
	    ctx->conv,
	    ctx->sink_queue,
	    ctx->mux,
	    ctx->sink,
	    NULL);

	/* Force change state to playing */
	p_debug("start playing\n");
	ret = gst_element_set_state(ctx->pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		p_err("Unable to set the pipeline to the playing state.\n");
		gst_object_unref(ctx->pipeline);
		return -1;
	}

	/* BUS watcher for debug */
	if (wfb_gst_init_bus(ctx) < 0) {
		p_err("Cannot initialize gst message bus.\n");
		return -1;
	}

	ctx->initialized = 1;

	return 0;
}

void
wfb_gst_context_deinit(struct wfb_gst_context *ctx)
{
	void *ret;

	pthread_mutex_lock(&ctx->lock);
	if (ctx->closing) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}
	pthread_mutex_unlock(&ctx->lock);

	pthread_cond_wait(&ctx->eos, &ctx->lock);

	ctx->closing = 1;
	gst_element_set_state(ctx->pipeline, GST_STATE_NULL);
	g_main_loop_quit(ctx->loop);
	gst_object_unref(ctx->pipeline);
	pthread_join(ctx->tid_loop, &ret);

	ctx->initialized = 0;
}

int
wfb_gst_thread_start(struct wfb_gst_context *ctx)
{
	if (!ctx->initialized) {
		p_debug("Decoder context is not initialized.\n");
		return -1;
	}
	pthread_create(&ctx->tid_loop, NULL, loop, ctx);

	return 0;
}

int
wfb_gst_thread_join(struct wfb_gst_context *ctx)
{
	wfb_gst_context_deinit(ctx);

	return 0;
}

void
wfb_gst(struct timespec *ts, uint8_t *data, size_t size, void *arg)
{
	struct wfb_gst_context *ctx = arg;
	GstBuffer *buf;
	int ret;

	assert(ctx);

	if (!ctx->initialized)
		return;
	if (ts == NULL) {
		gst_app_src_end_of_stream(GST_APP_SRC(ctx->source));
		return;
	}

	buf = gst_buffer_new_memdup(data, size);
	assert(buf);
	/*
	*/
	set_timestamp(buf, ts);

	gst_app_src_push_buffer(GST_APP_SRC(ctx->source), buf);
	// buf's ownerhsip is taken by library now.
#if 0
	GstStructure *st;
	g_object_get(ctx->rtp, "stats", &st, NULL);
	if (st) {
		 p_debug("%s\n", gst_structure_to_string(st));
	}
#endif

	return;
}
