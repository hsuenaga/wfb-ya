#ifndef __RX_CORE_H__
#define __RX_CORE_H__
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "frame_pcap.h"
#include "frame_radiotap.h"
#include "frame_ieee80211.h"
#include "frame_wfb.h"
#include "frame_udp.h"
#include "fec_wfb.h"

struct rx_mirror_handler {
	void (*func)(struct iovec *iov, int iovcnt, void *arg);
	void *arg;
};

struct rx_decode_handler {
	void (*func)(int8_t rssi, uint8_t *data, size_t size, void *arg);
	void *arg;
};

struct rx_log_handler {
	char file_name[PATH_MAX];
	int seq;
	FILE *fp;
};


struct rx_context {
	struct pcap_context pcap;
	struct radiotap_context radiotap;
	struct ieee80211_context ieee80211;
	struct wfb_context wfb;
	struct udp_context udp;
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

	/* meta data */
	struct sockaddr_in6 rx_src;
	uint16_t freq;
	int16_t dbm;

	/* data */
	struct rbuf *rx_ring;

	/* callback */
	struct rx_mirror_handler mirror_handler[RX_MAX_MIRROR];
	int n_mirror_handler;

	struct rx_decode_handler decode_handler[RX_MAX_DECODE];
	int n_decode_handler;

	/* log */
	struct rx_log_handler log_handler;
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

extern int rx_context_initialize(struct rx_context *ctx, uint32_t channel_id);
extern void rx_context_deinitialize(struct rx_context *ctx);
extern int rx_context_set_decode(struct rx_context *ctx,
    void (*decode)(int8_t, uint8_t *data, size_t size, void *arg),
    void *decode_arg);
extern void rx_decode_frame(int8_t rssi,
    struct rx_context *ctx, uint8_t *data, size_t size);
extern int rx_context_set_mirror(struct rx_context *ctx,
    void (*mirror)(struct iovec *iov, int iovcnt, void *arg), void *decode_arg);
extern void rx_mirror_frame(struct rx_context *ctx, uint8_t *data, size_t size);
extern void rx_context_dump(struct rx_context *ctx);
extern int rx_frame_pcap(struct rx_context *ctx, void *rxbuf, size_t rxlen);
extern int rx_frame_udp(struct rx_context *ctx, void *rxbuf, size_t rxlen);
#endif /* __RX_CORE_H__ */
