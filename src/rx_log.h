#ifndef __RX_LOG_H__
#define __RX_LOG_H__
#include <stdint.h>
#include <inttypes.h>
#include "rx_core.h"
#include "util_msg.h"

#define RX_LOG_VERSION		1
#define RX_LOG_SIGNATURE	0xdeadbeef

#define FRAME_TYPE_UNSPEC	0
#define FRAME_TYPE_INET6	1
#define FRAME_TYPE_DECODE	2
#define FRAME_TYPE_CORRUPT	3
#define FRAME_TYPE_MSG_INFO	252
#define FRAME_TYPE_MSG_ERR	253
#define FRAME_TYPE_MSG_DEBUG	254
#define FRAME_TYPE_MAX		255

#define DBM_INVAL		INT16_MIN
#define DBM_MIN			INT8_MIN
#define DBM_MAX			INT8_MAX

#define FRAME_SIZE_MIN		0
#define FRAME_SIZE_MAX		UINT32_MAX

struct rx_log_file_header {
	uint8_t version;
	uint8_t fec_type;
	uint8_t fec_k;
	uint8_t fec_n;
	uint32_t channel_id; // LE
	uint32_t signature; // LE
};

struct rx_log_frame_header {
	// LE
	uint64_t tv_sec;
	uint64_t tv_nsec;
	uint64_t seq; // act as key
	uint64_t block_idx;
	uint32_t size;
	uint8_t fragment_idx;
	uint8_t type;
	uint8_t pad1[2];

	// Host Byte order
	uint16_t freq;
	int16_t dbm;
	uint8_t pad2[4];

	// Network Byte order
	uint8_t rx_src[16]; // in6_addr
};

extern void rx_log_corrupt(struct rx_context *ctx);
extern void rx_log_frame(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, size_t size);
extern void rx_log_decode(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, uint8_t *data, size_t size);
extern void rx_log_create(struct rx_context *ctx);
extern int rx_log_hook(void *arg, enum msg_hook_type msg_type, const char *fmt, va_list ap);

#endif /* __RX_LOG_H__ */
