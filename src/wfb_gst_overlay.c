#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-info.h>

#include <cairo.h>
#include <cairo-gobject.h>

#include <glib.h>

#include "wfb_gst.h"
#include "wfb_gst_overlay.h"
#include "util_msg.h"

#include "compat.h"

#define FONT_NAME "Times New Roman"

typedef struct {
	gboolean valid;
	GstVideoInfo vinfo;
	struct wfb_gst_context *wfb_gst_ctx;

	/* graph parameters */
	int max_x;
	int max_y;
	double line_width;
	double text_size;
} CairoOverlayState;

static void
write_boundary(CairoOverlayState *s, cairo_t *cr)
{
	cairo_set_source_rgba(cr, 0.9, 0.0, 0.1, 0.7);
	cairo_set_line_width(cr, s->line_width);

	cairo_move_to(cr, 0, 0);
	cairo_line_to(cr, s->max_x, 0);
	cairo_line_to(cr, s->max_x, s->max_y);
	cairo_line_to(cr, 0, s->max_y);
	cairo_line_to(cr, 0, 0);
	cairo_stroke(cr);
}

static void
write_graph(CairoOverlayState *s, cairo_t *cr)
{
	struct wfb_gst_context *ctx = s->wfb_gst_ctx;

	cairo_set_source_rgba(cr, 0.9, 0.0, 0.1, 0.5);
	cairo_set_line_width(cr, s->line_width);

	cairo_move_to(cr, 0, s->max_y);
	for (int x = 0; x <= s->max_x; x++) {
		int idx;
		int y;

		idx = ctx->history_cur + x + 1;
		if (idx > NELEMS(ctx->history))
			idx = 0;
	       	y = ctx->history[x] + 65;	// add offset: -65dbm => 0
		y = s->max_y - y;		// invert y
		y = sat_y(y, 0, s->max_y);	// saturate y

		cairo_line_to(cr, x, y);
	}
	cairo_line_to(cr, s->max_x, s->max_y);
	cairo_line_to(cr, 0, s->max_y);
	cairo_fill(cr);
}

static void
write_text(CairoOverlayState *s, cairo_t *cr)
{
	struct wfb_gst_context *ctx = s->wfb_gst_ctx;
	char buf[16];
	int n;

	cairo_set_source_rgba(cr, 0.9, 0.9, 0.1, 1.0);
	cairo_set_line_width(cr, s->line_width);
	cairo_select_font_face(cr,
	    FONT_NAME, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size( cr, s->text_size );

	cairo_move_to(cr, 0, s->text_size);
	cairo_show_text(cr, "-15 dbm");

	cairo_move_to(cr, 0, s->max_y);
	cairo_show_text(cr, "-65 dbm");

	n = snprintf(buf, sizeof(buf),
	    "%d [dbm]", ctx->history[ctx->history_cur]);
	cairo_move_to(cr, (s->max_x - s->text_size * n)/2, s->text_size);
	cairo_show_text(cr, buf);
}

/* Store the information from the caps that we are interested in. */
static void
prepare_overlay (GstElement * overlay, GstCaps * caps, gpointer user_data)
{
	  CairoOverlayState *state = (CairoOverlayState *)user_data;

	    state->valid = gst_video_info_from_caps(&state->vinfo, caps);
}

/* Draw the overlay. 
 *  * This function draws a cute "beating" heart. */
static void
draw_overlay (GstElement * overlay, cairo_t * cr, guint64 timestamp,
		    guint64 duration, gpointer user_data)
{
	CairoOverlayState *s = (CairoOverlayState *)user_data;
	struct wfb_gst_context *ctx = s->wfb_gst_ctx;
	double scale;
	int width, height;
	int pos_x, pos_y, padding;
	int line_width, text_size;

	if (!s->valid)
		return;

	/* size of view port */
	width = GST_VIDEO_INFO_WIDTH(&s->vinfo);
	height = GST_VIDEO_INFO_HEIGHT(&s->vinfo);
	padding = 10;
	line_width = 1;
	text_size = 18;

	/* size of graph */
	s->max_x = NELEMS(ctx->history) - 1;
	s->max_y = 50;

	/* graph => viewport */
	scale = ((double)width - padding * 2) / (double)s->max_x;

	/* offset of view port */
	pos_x = padding;
	pos_y = height - padding - s->max_y * scale;

	/* line width and text size in graph */
	s->line_width = line_width / scale;
	s->text_size = text_size / scale;

	/* setup context */
	cairo_translate (cr, pos_x, pos_y);
	cairo_scale(cr, scale, scale);

	/* write it */
	write_boundary(s, cr);
	write_graph(s, cr);
	write_text(s, cr);
}

GstElement *
wfb_overlay(struct wfb_gst_context *ctx)
{
	GstElement *bin, *e, *last;
	GstPad *src, *sink;
	CairoOverlayState *overlay_state;

	assert(ctx);

	overlay_state = g_new0(CairoOverlayState, 1);
	if (overlay_state == NULL) {
		p_err("Cannot allocate state.\n");
		return NULL;
	}
	overlay_state->wfb_gst_ctx = ctx;

	bin = gst_bin_new("wfb_overlay_bin");
	if (bin == NULL) {
		p_err("Cannot allocate bin.\n");
		return NULL;
	}

	/* Convert video format */
	e = gst_element_factory_make("videoconvert", "wfb_overlay_vconv");
	if (e == NULL) {
		p_err("Cannot allocate videoconvert.\n");
		return NULL;
	}
	gst_bin_add(GST_BIN(bin), e);
	sink = gst_element_get_static_pad(e, "sink");
	if (sink == NULL) {
		p_err("Cannot get sink.\n");
		return NULL;
	}
	gst_element_add_pad(bin, gst_ghost_pad_new("sink", sink));
	last = e;

	/* Draw overlay */
	e = gst_element_factory_make("cairooverlay", "wfb_overlay");
	if (e == NULL) {
		p_err("Cannot create cairooverlay\n");
		return NULL;
	}
	g_signal_connect(e, "draw", G_CALLBACK(draw_overlay), overlay_state);
	g_signal_connect(e, "caps-changed",
	    G_CALLBACK(prepare_overlay), overlay_state);
	gst_bin_add(GST_BIN(bin), e);
	gst_element_link(last, e);
	src = gst_element_get_static_pad(e, "src");
	if (src == NULL) {
		p_err("Cannot get src.\n");
		return NULL;
	}
	gst_element_add_pad(bin, gst_ghost_pad_new("src", src));
	last = e;

	return bin;
}
