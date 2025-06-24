#ifndef __NET_INET6_H__
#define __NET_INET6_H__
#include <stdint.h>
#include <unistd.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <event2/event.h>

#include "wfb_params.h"
#include "net_core.h"
#include "rx_core.h"

struct netinet6_rx_context {
	struct netcore_context *net_ctx;
	struct rx_context *rx_ctx;
	const char *dev;
	struct sockaddr_storage *ss_local;
	bool multicast_rx;
	int rx_sock;
	struct event *rx_ev;

	uint8_t rxbuf[INET6_MTU];
};

struct netinet6_tx_context {
	struct netcore_context *net_ctx;
	const char *dev;
	int tx_sock;
};

extern int netinet6_rx_initialize(struct netinet6_rx_context *ctx,
    struct netcore_context *core_ctx,
    struct rx_context *rx_ctx,
    const char *dev);
extern int netinet6_tx_initialize(struct netinet6_tx_context *ctx,
    struct netcore_context *core_ctx, const char *dev);
extern void netinet6_rx_deinitialize(struct netinet6_rx_context *ctx);

extern void netinet6_tx(struct iovec *iov, int iovcnt, void *arg);
#endif /* __NET_INET6_H__ */
