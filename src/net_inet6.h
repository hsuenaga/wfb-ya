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

struct netinet6_context {
	struct netcore_context *net_ctx;
	struct rx_context *rx_ctx;
	int sock;
	struct event *ev;
	struct sockaddr_in6 mc_group;
	struct sockaddr_storage ss_src;
	int mc_if;

	uint8_t rxbuf[INET6_MTU];

	int (*cb)(void *data, size_t size, void *arg);
	void *cb_arg;
};

extern int netinet6_initialize(struct netinet6_context *ctx,
    struct netcore_context *core_ctx,
    struct rx_context *rx_ctx,
    const char *dev);
extern void netinet6_deinitialize(struct netinet6_context *ctx);

extern void netinet6_tx(struct iovec *iov, int iovcnt, void *arg);
#endif /* __NET_INET6_H__ */
