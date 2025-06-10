#ifndef __LOG_H265_H__
#define __LOG_H265_H__
#include "log_raw.h"
#include "wfb_gst.h"
void play_h265(struct log_store *ls);
void write_mp4(const char *file, struct log_store *ls);
#endif /* __LOG_H265_H__ */
