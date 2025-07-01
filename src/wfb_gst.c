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
#include "wfb_gst_overlay.h"
#include "util_msg.h"

#include "compat.h"
#include "wfb_params.h"

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

	assert(msg);

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

	assert(ctx);

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

	return state;
}

static int
change_state(struct wfb_gst_context *ctx, GstState state)
{
	GstStateChangeReturn rc;

	assert(ctx);

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

	assert(ctx);

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

	assert(ctx);

	state = GST_STATE(ctx->pipeline);

	return (state == GST_STATE_NULL);
}

static void
set_timestamp(GstBuffer *buf, struct timespec *ts)
{
	uint64_t nsec;

	assert(buf);
	assert(ts);

	nsec = ts->tv_sec * 1000 * 1000 * 1000;
	nsec += ts->tv_nsec;

	GST_BUFFER_TIMESTAMP(buf) = nsec;
	GST_BUFFER_PTS(buf) = nsec;
	GST_BUFFER_DTS(buf) = nsec;
	GST_BUFFER_DURATION(buf) = GST_CLOCK_TIME_NONE;
}

static void
close_session(struct wfb_gst_context *ctx, bool send_eof)
{
	assert(ctx);

	if (!ctx->initialized)
		return;

	pthread_mutex_lock(&ctx->lock);
	if (ctx->closing) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}
	ctx->closing = true;
	pthread_mutex_unlock(&ctx->lock);

	if (send_eof) {
		if (check_state(ctx) == GST_STATE_PLAYING) {
			gst_app_src_end_of_stream(GST_APP_SRC(ctx->source));
			pthread_mutex_lock(&ctx->lock);
			while (!ctx->eos_detected) {
				pthread_cond_wait(&ctx->eos, &ctx->lock);
			}
			pthread_mutex_unlock(&ctx->lock);
		}
	}

	ensure_state(ctx, GST_STATE_NULL);
	g_main_loop_quit(ctx->loop);
}

static void
join_thread(struct wfb_gst_context *ctx)
{
	void *ret;

	assert(ctx);

	pthread_mutex_lock(&ctx->lock);
	if (ctx->joined) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}
	ctx->joined = true;
	pthread_mutex_unlock(&ctx->lock);

	pthread_join(ctx->tid_loop, &ret);
	gst_object_unref(ctx->pipeline);
	ctx->initialized = 0;
}

static void *
loop(void *arg)
{
	struct wfb_gst_context *ctx = arg; 

	assert(ctx);

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

	assert(bus);
	assert(msg);
	assert(ctx);

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
			close_session(ctx, false);
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
			pthread_mutex_lock(&ctx->lock);
			pthread_cond_signal(&ctx->eos);
			ctx->eos_detected = true;
			pthread_mutex_unlock(&ctx->lock);
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
			p_debug("Ignore message %s -> %s\n",
			   GST_OBJECT_NAME(msg->src),
			   GST_MESSAGE_TYPE_NAME(msg));
			break;
		default:
			p_err("Unhandled message %s => %s\n",
			   GST_OBJECT_NAME(msg->src),
			   GST_MESSAGE_TYPE_NAME(msg));
			break;
	}

	return TRUE;
}

static int
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

static int
wfb_gst_init_source(struct wfb_gst_context *ctx,
    const char *file, bool enc, bool live)
{
	GstElement *appsrc, *queue;
	GstPad *src;
	GstCaps *caps;

	assert(ctx);

	ctx->source = gst_bin_new("wfb_appsrc_bin");

	appsrc = gst_element_factory_make("appsrc", "wfb_appsrc");
	if (appsrc  == NULL)
		return -1;
	queue = gst_element_factory_make("queue", "wfb_srcqueue");
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
	g_object_set(appsrc, "block", live ? FALSE : TRUE, NULL);
	g_object_set(appsrc, "emit-signals", FALSE, NULL);
	g_object_set(appsrc, "is-live", TRUE, NULL);
	g_object_set(appsrc, "do-timestamp", live ? TRUE : FALSE, NULL);
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

static int
wfb_gst_init_rtp(struct wfb_gst_context *ctx,
    const char *file, bool enc, bool live)
{
	GstElement *e, *first = NULL, *last = NULL;
	GstPad *src, *sink = NULL;

	assert(ctx);

	ctx->rtp = gst_bin_new("wfb_rtp_parser");

	if (live)
		goto skip_jitterbuf;

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
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

skip_jitterbuf:
	/* depayload */
	e = gst_element_factory_make("rtph265depay", "wfb_rtpdepay");
	if (e == NULL) {
		p_err("Cannot create h265 depayloader\n");
		return -1;
	}
	g_object_set(e, "auto-header-extension", TRUE, NULL);
	gst_bin_add(GST_BIN(ctx->rtp), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

	/* add pads */
	sink = gst_element_get_static_pad(first, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink pad from h265parse\n");
		return -1;
	}
	gst_element_add_pad(ctx->rtp, gst_ghost_pad_new("sink", sink));

	src = gst_element_get_static_pad(last, "src");
	if (src == NULL) {
		p_err("Cannot get src pad.\n");
		return -1;
	}
	gst_element_add_pad(ctx->rtp, gst_ghost_pad_new("src", src));

	return 0;
}


static int
wfb_gst_init_codec(struct wfb_gst_context *ctx,
    const char *file, bool enc, bool live)
{
	GstElement *e, *first = NULL, *last = NULL;
	GstPad *src, *sink;

	assert(ctx);

	ctx->codec = gst_bin_new("wfb_decoder");

	/* stream parser */
	e = gst_element_factory_make("h265parse", "wfb_h265");
	if (e == NULL)
		return -1;
	g_object_set(e, "config-interval", 1, NULL);
	gst_bin_add(GST_BIN(ctx->codec), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

	if (live)
		goto skip_ts;

	/* update timestamp */
	e = gst_element_factory_make("h265timestamper", "wfb_timestamp");
	if (e == NULL)
		return -1;
	gst_bin_add(GST_BIN(ctx->codec), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

	/* passthrough for file output */
	if (file && !enc) {
		p_info("Using h265parse(no decoding)\n");
		goto add_pad;
	}

skip_ts:
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

	/* fallback to pure software codec */
	e = gst_element_factory_make("libde265dec", "h265");
	if (e) {
		p_info("Uisng libde265dec\n");
		goto finish;
	}

	p_err("Cannot find H.265 codec.\n");
	return -1;
finish:
	gst_bin_add(GST_BIN(ctx->codec), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

add_pad:
	sink = gst_element_get_static_pad(first, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink pad.\n");
		return -1;
	}
	gst_element_add_pad(ctx->codec, gst_ghost_pad_new("sink", sink));

	src = gst_element_get_static_pad(last, "src");
	if (src == NULL) {
		p_err("Cannot get src pad.\n");
		return -1;
	}
	gst_element_add_pad(ctx->codec, gst_ghost_pad_new("src", src));

	return 0;
}

static int
wfb_gst_init_sink(struct wfb_gst_context *ctx,
    const char *file, bool enc, bool live)
{
	GstElement *e, *first = NULL, *last = NULL;
	GstPad *sink = NULL;

	assert(ctx);

	ctx->sink = gst_bin_new("wfb_sink");

	/* adjust video format */
	e = gst_element_factory_make("videoconvert", "wfb_vconv");
	if (e == NULL) {
		p_err("Cannot create videoconvert element.\n");
		return -1;
	}
	gst_bin_add(GST_BIN(ctx->sink), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

	/* Encoder */
	if (file && enc) {
		e = gst_element_factory_make("vtenc_h264", "wfb_encode");
		if (e) {
			p_info("Using vtenc_h264\n");
		}
		else {
			e = gst_element_factory_make("x264enc", "wfb_encode");
		}
		if (e) {
			p_info("Using x264enc\n");
		}
		else {
			p_err("Cannot create encoder\n");
			return -1;
		}

		gst_bin_add(GST_BIN(ctx->sink), e);
		first = first ? first : e;
		if (last) {
			gst_element_link(last, e);
		}
		last = e;
	}

	/* File Sink */
	if (file) {
		e = gst_element_factory_make("qtmux", "wfb_qtmux");
		if (e == NULL) {
			p_err("Cannot create qtmux\n");
			return -1;
		}
		gst_bin_add(GST_BIN(ctx->sink), e);
		first = first ? first : e;
		if (last) {
			gst_element_link(last, e);
		}
		last = e;

		e = gst_element_factory_make("filesink", "wfb_filesink");
		if (e == NULL) {
			p_err("Cannot create filesink\n");
			return -1;
		}
		g_object_set(e, "location", file, NULL);
		gst_bin_add(GST_BIN(ctx->sink), e);
		first = first ? first : e;
		if (last) {
			gst_element_link(last, e);
		}
		last = e;

		return 0;
	}

	/* Dispaly sink */
	e = gst_element_factory_make("queue", "wfb_sinkqueue");
	if (e == NULL) {
		p_err("Cannot allocate queue.\n");
		return -1;
	}
	gst_bin_add(GST_BIN(ctx->sink), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

	e = gst_element_factory_make("glimagesink", "wfb_videosink");
	if (e) {
		p_info("Use glimagesink.\n");
		goto finish;
	}
	e = gst_element_factory_make("osxvideosink", "wfb_videosink");
	if (e) {
		p_info("Use osxvideosink.\n");
		goto finish;
	}
	e = gst_element_factory_make("autovideosink", "wfb_videosink");
	if (e) {
		p_info("Use autovideosink.\n");
		goto finish;
	}
	p_err("Cannot create videosink\n");
	return -1;

finish:
	g_object_set(e, "sync", live ? FALSE : TRUE, NULL);
	gst_bin_add(GST_BIN(ctx->sink), e);
	first = first ? first : e;
	if (last) {
		gst_element_link(last, e);
	}
	last = e;

	/* Add pads */
	sink = gst_element_get_static_pad(first, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink pad from videoconvert.\n");
		return -1;
	}
	gst_element_add_pad(ctx->sink, gst_ghost_pad_new("sink", sink));

	return 0;
}

static int
wfb_gst_init_overlay(struct wfb_gst_context *ctx,
    const char *file, bool enc, bool live)
{
	assert(ctx);

	if (wfb_options.rssi_overlay)
		ctx->overlay = wfb_overlay(ctx);

	return 0;
}

int
wfb_gst_context_init(struct wfb_gst_context *ctx,
    const char *file, bool enc, bool live)
{
	GstElement *last;
	GstStateChangeReturn ret;

	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));
	ctx->file = file;
	ctx->enc = enc;

	pthread_mutex_init(&ctx->lock, NULL);
	pthread_cond_init(&ctx->eos, NULL);

	p_debug("Initializing gst\n");
	gst_init(NULL, NULL);

	p_debug("setup gst\n");

	/*
	 * Main thread
	 */
	/* AppSrc => queue */
	if (wfb_gst_init_source(ctx, file, enc, live) < 0) {
		p_err("failed to initialize gst source element.\n");
		return -1;
	}

	/*
	 * Runner thread
	 */
	/* queue => RTP parser */
	if (wfb_gst_init_rtp(ctx, file, enc, live) < 0) {
		p_err("failed to initialize gst rtp element.\n");
		return -1;
	}

	/* CODEC */
	if (wfb_gst_init_codec(ctx, file, enc, live) < 0) {
		p_err("failed to initialize gst codec element.\n");
		return -1;
	}

	/* Overlay */
	if (wfb_gst_init_overlay(ctx, file, enc, live) < 0) {
		p_err("failed to initialize overlay element.\n");
		return -1;
	}

	/* Output */
	if (wfb_gst_init_sink(ctx, file, enc, live) < 0) {
		p_err("failed to initialize gst sink element.\n");
		return -1;
	}

	/* Create pipeline with initialized elements above */
	ctx->pipeline = gst_pipeline_new("main-pipeline");

	p_debug("add and link elements\n");
	last = NULL;
	last = wfb_add_element(ctx->pipeline, ctx->source, last);
	last = wfb_add_element(ctx->pipeline, ctx->rtp, last);
	last = wfb_add_element(ctx->pipeline, ctx->codec, last);
	last = wfb_add_element(ctx->pipeline, ctx->overlay, last);
	last = wfb_add_element(ctx->pipeline, ctx->sink, last);

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

int
wfb_gst_context_init_live(struct wfb_gst_context *ctx)
{
	assert(ctx);

	return wfb_gst_context_init(ctx, NULL, false, true);
}

int
wfb_gst_context_init_play(struct wfb_gst_context *ctx)
{
	assert(ctx);

	return wfb_gst_context_init(ctx, NULL, false, false);
}

int
wfb_gst_context_init_write(struct wfb_gst_context *ctx, const char *file)
{
	assert(ctx);

	return wfb_gst_context_init(ctx, file, false, false);
}

int
wfb_gst_context_init_enc(struct wfb_gst_context *ctx, const char *file)
{
	assert(ctx);

	return wfb_gst_context_init(ctx, file, true, false);
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
	assert(ctx);

	close_session(ctx, true);
	join_thread(ctx);

	return 0;
}

void
wfb_gst_context_deinit(struct wfb_gst_context *ctx)
{
	assert(ctx);

	(void) wfb_gst_thread_join(ctx);
}

void
wfb_gst_add_dbm(struct wfb_gst_context *ctx, int8_t dbm)
{
	assert(ctx);

	pthread_mutex_lock(&ctx->lock);
	if (ctx->closing) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}

	ctx->history[ctx->history_cur++] = dbm;
	if (ctx->history_cur >= sizeof(ctx->history))
		ctx->history_cur = 0;

	pthread_mutex_unlock(&ctx->lock);
}

void
wfb_gst_write(struct wfb_gst_context *ctx,
    struct timespec *ts, uint8_t *data, size_t size)
{
	GstBuffer *buf;

	assert(ctx);

	if (!ctx->initialized)
		return;
	if (data == NULL || size == 0)
		return;

	pthread_mutex_lock(&ctx->lock);
	if (ctx->closing) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}
	pthread_mutex_unlock(&ctx->lock);

	change_state(ctx, GST_STATE_PLAYING);

	buf = gst_buffer_new_memdup(data, size);
	assert(buf);
	if (ts)
		set_timestamp(buf, ts);

	gst_app_src_push_buffer(GST_APP_SRC(ctx->appsrc), buf);
	// buf's ownerhsip is taken by library now.

	return;
}

void
wfb_gst_eos(struct wfb_gst_context *ctx)
{
	assert(ctx);

	pthread_mutex_lock(&ctx->lock);
	if (ctx->closing || is_stop(ctx)) {
		pthread_mutex_unlock(&ctx->lock);
		return;
	}
	gst_app_src_end_of_stream(GST_APP_SRC(ctx->appsrc));
	while (!ctx->eos_detected)
		pthread_cond_wait(&ctx->eos, &ctx->lock);
	pthread_mutex_unlock(&ctx->lock);
	ensure_state(ctx, GST_STATE_READY);
}

void
wfb_gst_handler(int8_t rssi, uint8_t *data, size_t size, void *arg)
{
	struct wfb_gst_context *ctx = (struct wfb_gst_context *)arg;

	assert(ctx);

	wfb_gst_add_dbm(ctx, rssi);
	wfb_gst_write(ctx, NULL, data, size);
}
