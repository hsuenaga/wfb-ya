#ifndef __FRAME_IEEE80211_H__
#define __FRAME_IEEE80211_H__
#include <stdint.h>
#include <unistd.h>
#include <sys/uio.h>

#include "util_attribute.h"

#define ETH_ADDR_LEN	6

#define FTYPE_MGMT		0x0
#define FTYPE_CTRL		0x1
#define FTYPE_DATA		0x2
#define FTYPE_EXT		0x3

#define DTYPE_DATA		0x0
#define DTYPE_NULL		0x4
#define DTYPE_QOS		0x8
#define DTYPE_QOS_ACK		0x9
#define DTYPE_QOS_POLL		0xa
#define DTYPE_QOS_ACK_POLL	0xb
#define DTYPE_QOS_NULL		0xc
#define DTYPE_QOS_NULL_POLL	0xe
#define DTYPE_QOS_NULL_ACK_POLL	0xf

struct ieee80211_header {
	uint16_t frame_control;
	uint16_t duration_id;
	union {
		struct {
			uint8_t addr1[ETH_ADDR_LEN];
			uint8_t addr2[ETH_ADDR_LEN];
			uint8_t addr3[ETH_ADDR_LEN];
			uint16_t seq_ctrl;
		} base3 __packed;
		struct {
			uint8_t addr1[ETH_ADDR_LEN];
			uint8_t addr2[ETH_ADDR_LEN];
			uint8_t addr3[ETH_ADDR_LEN];
			uint16_t seq_ctrl;
			uint16_t qos_ctrl;
		} qos3 __packed;
		struct {
			uint8_t addr1[ETH_ADDR_LEN];
			uint8_t addr2[ETH_ADDR_LEN];
			uint8_t addr3[ETH_ADDR_LEN];
			uint16_t seq_ctrl;
			uint8_t addr4[ETH_ADDR_LEN];
			uint16_t qos_ctrl;
		} qos4 __packed;
		struct {
			uint8_t ra[ETH_ADDR_LEN];
			uint8_t ta[ETH_ADDR_LEN];
			uint64_t common_info;
		} trigger __packed;
	} u;
} __packed;

#define IEEE80211_DATA_HDRLEN \
    (offsetof(struct ieee80211_header, u.base3.seq_ctrl) + sizeof(uint16_t))

struct ieee80211_context {
	struct ieee80211_header *hdr;
	size_t hdrlen;
	uint16_t wfb_signature;
	uint32_t channel_id;
};

struct ieee80211_tx_context {
	struct ieee80211_header hdr;
	struct iovec tx_iov;
};

extern void ieee80211_tx_context_initialize(struct ieee80211_tx_context *ctx,
    uint8_t *dst, uint8_t *src, uint8_t *bssid);
extern void ieee80211_context_dump(const struct ieee80211_context *ctx);
extern ssize_t ieee80211_frame_parse(void *data, size_t size,
    struct ieee80211_context *ctx);
#endif /* __FRAME_IEEE80211_H__ */
