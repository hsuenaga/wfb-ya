#ifndef __FRAME_IEEE80211_H__
#define __FRAME_IEEE80211_H__
#include <stdint.h>

#define ETH_ADDR_LEN	6
#define WFG_SIG		0x5742

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
	uint16_t dulation_id;
	union {
		struct {
			uint8_t addr1[ETH_ADDR_LEN];
			uint8_t addr2[ETH_ADDR_LEN];
			uint8_t addr3[ETH_ADDR_LEN];
			uint16_t seq_ctrl;
			uint8_t variable[];
		} base3 __attribute__((__packed__));
		struct {
			uint8_t addr1[ETH_ADDR_LEN];
			uint8_t addr2[ETH_ADDR_LEN];
			uint8_t addr3[ETH_ADDR_LEN];
			uint16_t seq_ctrl;
			uint16_t qos_ctrl;
			uint8_t variable[];
		} qos3 __attribute__((__packed__));
		struct {
			uint8_t addr1[ETH_ADDR_LEN];
			uint8_t addr2[ETH_ADDR_LEN];
			uint8_t addr3[ETH_ADDR_LEN];
			uint16_t seq_ctrl;
			uint8_t addr4[ETH_ADDR_LEN];
			uint16_t qos_ctrl;
			uint8_t variable[];
		} qos4 __attribute__((__packed__));
		struct {
			uint8_t ra[ETH_ADDR_LEN];
			uint8_t ta[ETH_ADDR_LEN];
			uint64_t common_info;
			uint8_t variable[];
		} trigger __attribute__((__packed__));
	} u;
} __attribute__((__packed__));

#define IEEE80211_DATA_HDRLEN \
    (offsetof(struct ieee80211_header, u.base3.variable))

struct ieee80211_context {
	uint8_t dst[ETH_ADDR_LEN];
	uint8_t src[ETH_ADDR_LEN];
	uint8_t bss[ETH_ADDR_LEN];
	uint16_t wfb_signature;
	uint32_t channel_id;
	uint8_t stream_id;
};

extern void ieee80211_context_dump(const struct ieee80211_context *ctx);
extern ssize_t ieee80211_frame_parse(void *data, size_t size,
    struct ieee80211_context *ctx);
#endif /* __FRAME_IEEE80211_H__ */
