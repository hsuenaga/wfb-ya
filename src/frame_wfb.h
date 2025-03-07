#ifndef __FRAME_WFB_H__
#define __FRAME_WFB_H__
#include <sodium.h>

#include "net_pcap.h"
#include "frame_ieee80211.h"
#include "util_attribute.h"

#define WFB_PACKET_DATA		0x01
#define WFB_PACKET_SESSION	0x02

struct wfb_data_hdr {
	uint8_t flags;
	uint16_t packet_size;
} __packed;
#define WFB_DATA_HDRLEN (sizeof(struct wfb_data_hdr))

struct wfb_session_data {
	uint64_t epoch;
	uint32_t channel_id;
	uint8_t fec_type;
	uint8_t fec_k;
	uint8_t fec_n;
	uint8_t session_key[crypto_aead_chacha20poly1305_KEYBYTES];
	uint8_t tags[];
} __packed;
#define WFB_SESSION_DATALEN (sizeof(struct wfb_session_data))

struct wfb_ng_hdr {
	uint8_t packet_type;
	union {
		struct {
			uint8_t nonce[crypto_aead_chacha20poly1305_NPUBBYTES];

			// encrypted by session-key
			struct wfb_data_hdr hdr;
			uint8_t payload[];
		} data;
		struct {
			uint8_t nonce[crypto_box_NONCEBYTES];

			// encrypted by public-key
			struct wfb_session_data data;
		} session;
	} u;
} __packed;
#define WFB_DATA_BLOCK_HDRLEN (offsetof(struct wfb_ng_hdr, u.data.hdr))
#define WFB_SESSION_BLOCK_HDRLEN (offsetof(struct wfb_ng_hdr, u.session.data))

#define MIN_DATA_PACKET_LEN \
    (WFB_DATA_HDRLEN + crypto_aead_chacha20poly1305_ABYTES)
#define MIN_SESSION_PACKET_LEN \
    (WFB_SESSION_DATALEN + crypto_box_MACBYTES)

#define MAX_DATA_PACKET_SIZE (WIFI_MTU - IEEE80211_DATA_HDRLEN)
#define MAX_SESSION_PACKET_SIZE (WIFI_MTU - IEEE80211_DATA_HDRLEN)
#define MAX_FEC_PAYLOAD (WIFI_MTU - IEEE80211_DATA_HDRLEN - \
    WFB_DATA_BLOCK_HDRLEN - crypto_aead_chacha20poly1305_ABYTES)

#define RX_RING_SIZE 40
#define MAX_BLOCK_IDX ((1LLU << 55) - 1)

#define WFB_FEC_VDM_RS 1

struct wfb_context {
	struct wfb_ng_hdr *hdr;
	size_t hdrlen;
	size_t pktlen;
	unsigned long long cipherlen;
	uint8_t *nonce;
	size_t noncelen;
	uint8_t *cipher;
	uint64_t block_idx;
	uint8_t fragment_idx;
};

extern void wfb_context_dump(const struct wfb_context *ctx);
extern ssize_t wfb_frame_parse(void *data, size_t size,
    struct wfb_context *ctx);

#endif /* __FRAME_WFB_H__ */
