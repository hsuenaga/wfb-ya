#ifndef __FRAME_RADIOTAP_H__
#define __FRAME_RADIOTAP_H__
#include <stdint.h>
#include <stdbool.h>

struct radiotap_mcs {
	uint8_t known;
	uint8_t flags;
	uint8_t index;
};

struct radiotap_channel {
	uint16_t freq;
	uint16_t flags;
};

struct radiotap_context {
	uint64_t		tsft;
	uint8_t			flags;
	struct radiotap_channel	channel;
	int8_t			dbm_signal;
	uint8_t			antenna;
	uint16_t		rx_flags;
	struct radiotap_mcs	mcs;

	bool			has_fcs;
	bool			bad_fcs;
};

extern void radiotap_context_dump(const struct radiotap_context *ctx);
extern ssize_t radiotap_frame_parse(void *data, size_t size,
    struct radiotap_context *ctx);
#endif /* __FRAME_RADIOTAP_H__ */
