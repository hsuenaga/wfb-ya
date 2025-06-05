#ifndef __RX_CORE_H__
#define __RX_CORE_H__
#include <stdbool.h>
#include <time.h>

#include "frame_pcap.h"
#include "frame_radiotap.h"
#include "frame_ieee80211.h"
#include "frame_wfb.h"
#include "fec_wfb.h"

struct rx_handler {
	void (*func)(uint8_t *data, size_t size, void *arg);
	void *arg;
};

struct rx_logger {
	FILE *fp;
};

struct rx_log_header {
	// LE
	uint64_t seq; // act as key
	struct timespec ts;
	uint32_t size;
	uint64_t block_idx;
	uint8_t fragment_idx;
	uint8_t fec_k;
	uint8_t fec_n;
};

struct rx_context {
	struct pcap_context pcap;
	struct radiotap_context radiotap;
	struct ieee80211_context ieee80211;
	struct wfb_context wfb;
	struct fec_context fec;

	/* common */
	uint32_t channel_id;

	/* session */
	uint64_t epoch;
	uint8_t fec_type;
	uint8_t fec_k;
	uint8_t fec_n;
	uint8_t session_key[crypto_aead_chacha20poly1305_KEYBYTES];
	bool has_session_key;

	/* data */
	struct rbuf *rx_ring;

	/* callback */
	struct rx_handler mirror_handler[RX_MAX_MIRROR];
	int n_mirror_handler;

	struct rx_handler decode_handler[RX_MAX_DECODE];
	int n_decode_handler;

	struct rx_logger log_handler;
};

static inline uint64_t
rx_get_seq_outer(struct rx_context *ctx)
{
	uint64_t seq;

	if (ctx->wfb.hdr->packet_type != WFB_PACKET_DATA)
		return 0;

	seq = ctx->wfb.block_idx * ctx->fec_n + ctx->wfb.fragment_idx;

	return seq;
}

extern int rx_context_init(struct rx_context *ctx, uint32_t channel_id);
extern int rx_context_set_decode(struct rx_context *ctx,
    void (*decode)(uint8_t *data, size_t size, void *arg), void *decode_arg);
extern void rx_decode_frame(struct rx_context *ctx, uint8_t *data, size_t size);
extern int rx_context_set_mirror(struct rx_context *ctx,
    void (*mirror)(uint8_t *data, size_t size, void *arg), void *decode_arg);
extern void rx_mirror_frame(struct rx_context *ctx, uint8_t *data, size_t size);
extern void rx_log_frame(struct rx_context *ctx,
    uint64_t block_idx, uint8_t fragment_idx, uint8_t *data, size_t size);
extern void rx_log_create(struct rx_context *ctx);
extern void rx_context_dump(struct rx_context *ctx);
extern int rx_frame_pcap(struct rx_context *ctx, void *rxbuf, size_t rxlen);
extern int rx_frame_udp(struct rx_context *ctx, void *rxbuf, size_t rxlen);
#endif /* __RX_CORE_H__ */
