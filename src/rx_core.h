#ifndef __RX_CORE_H__
#define __RX_CORE_H__
#include <stdbool.h>

#include "frame_pcap.h"
#include "frame_radiotap.h"
#include "frame_ieee80211.h"
#include "frame_wfb.h"
#include "fec_wfb.h"

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
	void (*cb)(uint8_t *data, size_t size, void *arg);
	void *cb_arg;
};

extern int rx_context_init(struct rx_context *ctx, uint32_t channel_id,
    void (*cb)(uint8_t *data, size_t size, void *arg), void *arg);
extern void rx_context_dump(struct rx_context *ctx);
extern int rx_frame_pcap(struct rx_context *ctx, void *rxbuf, size_t rxlen);
extern int rx_frame_udp(struct rx_context *ctx, void *rxbuf, size_t rxlen);
#endif /* __RX_CORE_H__ */
