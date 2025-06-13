#ifndef __FRAME_UDP_H__
#define __FRAME_UDP_H__
#include "util_attribute.h"

#define UDP_FLAG_CORRUPT	(0x1)

struct wfb_udp_header {
	// Network byte order
	uint32_t flags;
	uint16_t freq;
	int16_t dbm;
} __packed;

struct udp_context {
	struct wfb_udp_header raw;
	struct wfb_udp_header *hdr;
	size_t hdrlen;

	uint32_t flags;
	uint16_t freq;
	int16_t dbm;
};

extern void udp_context_dump(struct udp_context *ctx);
extern ssize_t udp_frame_parse(void *data, size_t size,
    struct udp_context *ctx);
extern ssize_t udp_frame_build(struct udp_context *ctx);

#endif /* __FRAME_UDP_H__ */
