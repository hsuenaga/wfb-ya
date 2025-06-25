#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include "wfb_gst.h"
#include "util_msg.h"

static const char *
s_state(GstState state)
{
	switch (state) {
		case GST_STATE_VOID_PENDING:
			return "PENDING";
		case GST_STATE_NULL:
			return "NULL";
		case GST_STATE_READY:
			return "READY";
		case GST_STATE_PAUSED:
			return "PAUSED";
		case GST_STATE_PLAYING:
			return "PLAYING";
		default:
			break;
	}

	return "(Unknown)";
}

static void
handle_state_changed(GstMessage *msg)
{
	GstState old_state, new_state;

	gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
	p_debug("Element %s changed state from %s to %s.\n",
	    GST_OBJECT_NAME (msg->src),
	    gst_element_state_get_name (old_state),
	    gst_element_state_get_name (new_state));
}

static GstState
check_state(struct wfb_gst_context *ctx)
{
	GstStateChangeReturn rc;
	GstState state, pending;
	GstClockTime to = 1000 * 1000; /* [ns] */

	p_err("Check\n");
retry:
	rc = gst_element_get_state(ctx->pipeline,
	    &state, &pending, to);
	if (rc == GST_STATE_CHANGE_FAILURE) {
		p_err("Cannot connect to GStreamer.\n");
		return GST_STATE_NULL;
	}
	if (pending != GST_STATE_VOID_PENDING) {
		p_err("%s => %s\n", s_state(state), s_state(pending));
		goto retry;
	}
	p_err("OK\n");

	return state;
}

static int
change_state(struct wfb_gst_context *ctx, GstState state)
{
	GstStateChangeReturn rc;

	if (GST_STATE(ctx->pipeline) == state)
		return 0;

	rc = gst_element_set_state(ctx->pipeline, state);
	if (rc == GST_STATE_CHANGE_FAILURE) {
		p_err("Cannot connect to GStreamer.\n");
		return -1;
	}

	return 0;
}

static int
ensure_state(struct wfb_gst_context *ctx, GstState state)
{
	GstStateChangeReturn rc;

	if (GST_STATE(ctx->pipeline) == state)
		return 0;

	rc = gst_element_set_state(ctx->pipeline, state);
	if (rc == GST_STATE_CHANGE_FAILURE) {
		p_err("Cannot connect to GStreamer.\n");
		return -1;
	}
	if (rc == GST_STATE_CHANGE_ASYNC) {
		if (check_state(ctx) != state) {
			p_err("Inconsistence state\n");
			return -1;
		}
	}

	return 0;
}

static bool
is_stop(struct wfb_gst_context *ctx)
{
	GstState state;

	state = GST_STATE(ctx->pipeline);

	return (state == GST_STATE_NULL);
}

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
	GstClock *clock = NULL; 
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
		case GST_MESSAGE_NEW_CLOCK:
			gst_message_parse_new_clock(msg, &clock);
			if (clock) {
				p_info("ClockName: %s\n", GST_OBJECT_NAME(clock));
			}
			else {
				p_info("ClockName: NULL\n");
			}
			break;
		case GST_MESSAGE_STATE_CHANGED:
			handle_state_changed(msg);
			break;
		case GST_MESSAGE_STREAM_STATUS:
		case GST_MESSAGE_STREAM_START:
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
	GstElement *appsrc, *queue;
	GstPad *src;
	GstCaps *caps;

	assert(ctx);

	ctx->source = gst_bin_new("wfb_appsrc_bin");

	appsrc = gst_element_factory_make("appsrc", "wfb_appsrc");
	if (appsrc  == NULL)
		return -1;
	queue = gst_element_factory_make("queue", "wfb_queue");
	if (queue == NULL)
		return -1;

	/* configure appsrc */
	gst_util_set_object_arg (G_OBJECT (appsrc), "format", "time");
	caps = gst_caps_new_simple("application/x-rtp",
			"media", G_TYPE_STRING, "video",
			"clock-rate", G_TYPE_INT, 90000,
			"encoding-name", G_TYPE_STRING, "H265",
			"framerate", GST_TYPE_FRACTION, 120, 1,
			NULL);
	if (caps == NULL)
		return -1;

	g_object_set(appsrc, "caps", caps, NULL);
	g_object_set(appsrc, "block", TRUE, NULL);
	g_object_set(appsrc, "emit-signals", FALSE, NULL);
	g_object_set(appsrc, "is-live", TRUE, NULL);
	g_object_set(appsrc, "do-timestamp", FALSE, NULL);
	gst_caps_unref(caps);

	gst_bin_add(GST_BIN(ctx->source), appsrc);
	gst_bin_add(GST_BIN(ctx->source), queue);
	gst_element_link(appsrc, queue);

	/* cerate pad */
	src = gst_element_get_static_pad(queue, "src");
	if (src == NULL) {
		p_err("Cannot get src pad from queue\n");
		return -1;
	}
	gst_element_add_pad(ctx->source, gst_ghost_pad_new("src", src));

	ctx->appsrc = appsrc;

	return 0;
}

int
wfb_gst_init_codec(struct wfb_gst_context *ctx, const char *file, bool enc)
{
	GstElement *e, *last = NULL;
	GstPad *src, *sink;
	assert(ctx);

	ctx->codec = gst_bin_new("wfb_decoder");

	/* stream parser */
	e = gst_element_factory_make("h265parse", "wfb_h265");
	if (e == NULL)
		return -1;
	g_object_set(e, "config-interval", 1, NULL);
	gst_bin_add(GST_BIN(ctx->codec), e);
	sink = gst_element_get_static_pad(e, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink pad from h265parse\n");
		return -1;
	}
	gst_element_add_pad(ctx->codec, gst_ghost_pad_new("sink", sink));
	last = e;

	/* update timestamp */
	e = gst_element_factory_make("h265timestamper", "wfb_timestamp");
	if (e == NULL)
		return -1;
	gst_bin_add(GST_BIN(ctx->codec), e);
	gst_element_link(last, e);
	last = e;

	/* passthrough for file output */
	if (file && !enc) {
		p_info("Using h265parse\n");
		goto add_pad;
	}

	/* try Video4Linux */
	e = gst_element_factory_make("v4l2slh265dec", "h265");
	if (e) {
		p_info("Uisng v4l2slh265dec\n");
		goto finish;
	}

	/* try VideoToolkit */
	e = gst_element_factory_make("vtdec", "h265");
	if (e) {
		g_object_set(e, "discard-corrupted-frames", FALSE, NULL);
		p_info("Using vtdec\n");
		goto finish;
	}

	/* try ffmpeg */
	e = gst_element_factory_make("avdec_h265", "h265");
	if (e) {
		p_info("Uisng avdec_h265\n");
		goto finish;
	}
	
	/* fallback to software codec */
	e = gst_element_factory_make("libde265dec", "h265");
	if (e) {
		p_info("Uisng libde265dec\n");
		goto finish;
	}

	p_err("Cannot find H.265 codec.\n");
	return -1;
finish:
	gst_bin_add(GST_BIN(ctx->codec), e);
	gst_element_link(last, e);
add_pad:
	src = gst_element_get_static_pad(e, "src");
	if (src == NULL) {
		p_err("Cannot get src pad.\n");
		return -1;
	}
	gst_element_add_pad(ctx->codec, gst_ghost_pad_new("src", src));

	return 0;
}

int
wfb_gst_init_rtp(struct wfb_gst_context *ctx, const char *file, bool enc)
{
	GstElement *e, *last = NULL;
	GstPad *src, *sink;
	assert(ctx);

	ctx->rtp = gst_bin_new("wfb_rtp_parser");

	/* remove jitters */
	e = gst_element_factory_make("rtpjitterbuffer", "wfb_jitbuf");
	if (e == NULL) {
		p_err("Cannot create jitter buffer\n");
		return -1;
	}
	g_object_set(e, "latency", 10, NULL); // [ms]
	gst_bin_add(GST_BIN(ctx->rtp), e);
	sink = gst_element_get_static_pad(e, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink pad from h265parse\n");
		return -1;
	}
	gst_element_add_pad(ctx->rtp, gst_ghost_pad_new("sink", sink));
	last = e;

	/* depayload */
	e = gst_element_factory_make("rtph265depay", "wfb_rtpdepay");
	if (e == NULL) {
		p_err("Cannot create h265 depayloader\n");
		return -1;
	}
	g_object_set(e, "auto-header-extension", TRUE, NULL);
	gst_bin_add(GST_BIN(ctx->rtp), e);
	gst_element_link(last, e);
	last = e;

	/* add src pad */
	src = gst_element_get_static_pad(e, "src");
	if (src == NULL) {
		p_err("Cannot get src pad.\n");
		return -1;
	}
	gst_element_add_pad(ctx->rtp, gst_ghost_pad_new("src", src));

	return 0;
}

int
wfb_gst_init_sink(struct wfb_gst_context *ctx, const char *file, bool enc)
{
	GstElement *e, *last = NULL;
	GstPad *src, *sink;
	assert(ctx);

	ctx->sink = gst_bin_new("wfb_sink");

	/* adjust video format */
	e = gst_element_factory_make("videoconvert", "wfb_vconv");
	if (e == NULL) {
		p_err("Cannot create videoconvert element.\n");
		return -1;
	}
	gst_bin_add(GST_BIN(ctx->sink), e);
	sink = gst_element_get_static_pad(e, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink pad from videoconvert.\n");
		return -1;
	}
	gst_element_add_pad(ctx->sink, gst_ghost_pad_new("sink", sink));
	last = e;

	/* file sink */
	if (file && enc) {
		e = gst_element_factory_make("vtenc_h264", "wfb_encode");
		if (e) {
			p_info("Using vtenc_h264\n");
		}
		else {
			e = gst_element_factory_make("x264enc", "wfb_encode");
			if (e) {
				p_info("Using x264enc\n");
			}
		}
		if (e == NULL) {
			p_err("Cannot create encoder\n");
			return -1;
		}
		gst_bin_add(GST_BIN(ctx->sink), e);
		gst_element_link(last, e);
		last = e;
	}
	if (file) {
		e = gst_element_factory_make("qtmux", "wfb_qtmux");
		if (e == NULL) {
			p_err("Cannot create qtmux\n");
			return -1;
		}
		gst_bin_add(GST_BIN(ctx->sink), e);
		gst_element_link(last, e);
		last = e;

		e = gst_element_factory_make("filesink", "wfb_filesink");
		if (e == NULL) {
			p_err("Cannot create filesink\n");
			return -1;
		}
		g_object_set(e, "location", file, NULL);
		gst_bin_add(GST_BIN(ctx->sink), e);
		gst_element_link(last, e);
		last = e;

		return 0;
	}

	/* dispaly sink */
	e = gst_element_factory_make("osxvideosink", "wfb_videosink");
	if (e == NULL) {
		e = gst_element_factory_make("glimagesink", "wfb_videosink");
	}
	if (e == NULL) {
		e = gst_element_factory_make("autovideosink", "wfb_videosink");
	}
	if (e == NULL) {
		p_err("Cannot create videosink\n");
		return -1;
	}
	g_object_set(e, "sync", TRUE, NULL);
	gst_bin_add(GST_BIN(ctx->sink), e);
	gst_element_link(last, e);
	last = e;

	return 0;
}

int
wfb_gst_context_init(struct wfb_gst_context *ctx, const char *file, bool enc)
{
	GstElement *last = NULL;
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

	/* RTP parser */
	if (wfb_gst_init_rtp(ctx, file, enc) < 0) {
		p_err("failed to initialize gst rtp element.\n");
		return -1;
	}

	/* CODEC */
	if (wfb_gst_init_codec(ctx, file, enc) < 0) {
		p_err("failed to initialize gst codec element.\n");
		return -1;
	}

	/* Output */
	if (wfb_gst_init_sink(ctx, file, enc) < 0) {
		p_err("failed to initialize gst sink element.\n");
		return -1;
	}

	/* Create pipeline with initialized elements above */
	ctx->pipeline = gst_pipeline_new("main-pipeline");

	p_debug("add and link elements\n");
	if (ctx->source) {
		gst_bin_add(GST_BIN(ctx->pipeline), ctx->source);
		last = ctx->source;
	}
	if (ctx->rtp) {
		gst_bin_add(GST_BIN(ctx->pipeline), ctx->rtp);
		gst_element_link(last, ctx->rtp);
		last = ctx->rtp;
	}
	if (ctx->codec) {
		gst_bin_add(GST_BIN(ctx->pipeline), ctx->codec);
		gst_element_link(last, ctx->codec);
		last = ctx->codec;
	}
	if (ctx->overlay) {
		gst_bin_add(GST_BIN(ctx->pipeline), ctx->overlay);
		gst_element_link(last, ctx->overlay);
		last = ctx->overlay;
	}
	if (ctx->sink) {
		gst_bin_add(GST_BIN(ctx->pipeline), ctx->sink);
		gst_element_link(last, ctx->sink);
		last = ctx->sink;
	}

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

	if (check_state(ctx) == GST_STATE_PLAYING) {
		gst_app_src_end_of_stream(GST_APP_SRC(ctx->source));
		pthread_cond_wait(&ctx->eos, &ctx->lock);
	}

	ctx->closing = 1;
	ensure_state(ctx, GST_STATE_NULL);
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
wfb_gst_write(struct timespec *ts, uint8_t *data, size_t size, void *arg)
{
	struct wfb_gst_context *ctx = arg;
	GstBuffer *buf;
	int ret;

	assert(ctx);

	if (!ctx->initialized)
		return;
	if (ts == NULL || data == NULL || size == 0)
		return;

	change_state(ctx, GST_STATE_PLAYING);

	buf = gst_buffer_new_memdup(data, size);
	assert(buf);
	set_timestamp(buf, ts);

	gst_app_src_push_buffer(GST_APP_SRC(ctx->appsrc), buf);
	// buf's ownerhsip is taken by library now.

	return;
}

void
wfb_gst_eos(void *arg)
{
	struct wfb_gst_context *ctx = (struct wfb_gst_context *)arg;

	assert(ctx);

	if (is_stop(ctx))
		return;

	gst_app_src_end_of_stream(GST_APP_SRC(ctx->appsrc));
	pthread_cond_wait(&ctx->eos, &ctx->lock);
	ensure_state(ctx, GST_STATE_READY);
}
