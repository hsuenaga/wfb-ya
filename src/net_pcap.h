#ifndef __NET_PCAP_H__
#define __NET_PCAP_H__
#include <stdint.h>
#include <stdbool.h>
#include <pcap.h>
#include <event2/event.h>

#include "wfb_params.h"
#include "net_core.h"
#include "rx_core.h"

struct netpcap_context {
	struct netcore_context *net_ctx;
	struct rx_context *rx_ctx;

	pcap_t *pcap;
	int fd;
	struct event *ev;
};

extern int netpcap_initialize(struct netpcap_context *ctx,
    struct netcore_context *net_ctx,
    struct rx_context *rx_ctx,
    const char *dev, uint32_t channel_id, bool use_monitor);
extern void netpcap_deinitialize(struct netpcap_context *ctx);

#endif /* __NET_PCAP_H__ */
