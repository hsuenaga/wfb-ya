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

	/* session */
	uint64_t epoch;
	uint8_t fec_type;
	uint8_t fec_k;
	uint8_t fec_n;
	uint8_t session_key[crypto_aead_chacha20poly1305_KEYBYTES];
	bool has_session_key;

	/* data */
	struct rbuf *rx_ring;
};

extern int rx_context_init(struct rx_context *ctx);
extern void rx_context_dump(struct rx_context *ctx);
extern int rx_frame(void *rxbuf, size_t rxlen, void *arg);
#endif /* __RX_CORE_H__ */
