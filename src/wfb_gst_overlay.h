#ifndef __WFB_GST_OVERLAY_H__
#define __WFB_GST_OVERLAY_H__
#include <gst/gst.h>
#include "wfb_gst.h"

extern GstElement *wfb_overlay(struct wfb_gst_context *ctx);

static inline int
sat_y(int y, int min_y, int max_y)
{
	if (y > max_y)
		return max_y;
	if (y < min_y)
		return min_y;

	return y;
}
#endif /* __WFB_GST_OVERLAY_H__ */
