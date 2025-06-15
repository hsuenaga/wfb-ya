#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <assert.h>

#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>

#include "compat.h"

#include "wfb_params.h"
#include "net_inet6.h"
#include "util_msg.h"

static void
netinet6_rx(evutil_socket_t fd, short event, void *arg)
{
	struct netinet6_rx_context *ctx = (struct netinet6_rx_context *)arg;
	struct sockaddr_storage ss_src;
	socklen_t ss_len;
	ssize_t rxlen;

	assert(ctx);

retry:
	ss_len = sizeof(ss_src);
	rxlen = recvfrom(fd, ctx->rxbuf, sizeof(ctx->rxbuf), 0,
	    (struct sockaddr *)&ss_src, &ss_len);
	if (rxlen < 0) {
		if (errno == EINTR) {
			goto retry;
		}
		p_debug("recvfrom() failed: %s.\n", strerror(errno));
		netcore_reload(ctx->net_ctx);
		return;
	}

	switch (ss_src.ss_family) {
		case AF_INET6:
			memcpy(&ctx->rx_ctx->rx_src, &ss_src, ss_len);
			break;
		default:
			ctx->rx_ctx->rx_src.sin6_family = AF_UNSPEC;
			break;
	}

	rx_frame_udp(ctx->rx_ctx, ctx->rxbuf, rxlen);
}


void
netinet6_tx(struct iovec *iov, int iovcnt, void *arg)
{
	struct netinet6_tx_context *ctx = (struct netinet6_tx_context *)arg;
	ssize_t n;

	assert(ctx);
	assert(iov);
	assert(iovcnt > 0);

retry:
	/*
	n = sendto(ctx->sock, data, size, MSG_DONTWAIT,
	    (struct sockaddr *)&ctx->mc_group, sizeof(ctx->mc_group));
        */
	n = writev(ctx->tx_sock, iov, iovcnt);
	if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		}
		p_err("writev() failed: %s\n", strerror(errno));
		netcore_reload(ctx->net_ctx);
	}
}

static int
netinet6_rx_socket_open(void *arg)
{
	struct netinet6_rx_context *ctx = (struct netinet6_rx_context *)arg;
	struct ipv6_mreq mreq;
	struct sockaddr_in6 sin6_any;
	struct sockaddr_in6 mc_group;
	const int enable = 1;
	int mc_if;
	int s, r;

	assert(ctx);

	if (ctx->rx_ev) {
		netcore_rx_event_del(ctx->net_ctx, ctx->rx_ev);
		ctx->rx_ev = NULL;
	}
	if (ctx->rx_sock >= 0) {
		close(ctx->rx_sock);
		ctx->rx_sock = -1;
	}

#ifdef SOCK_CLOEXEC
	s = socket(AF_INET6, SOCK_DGRAM|SOCK_CLOEXEC, 0);
#else
	s = socket(AF_INET6, SOCK_DGRAM, 0);
#endif
	if (s < 0) {
		p_err("Socket() failed: %s\n", strerror(errno));
		return -1;
	}

	/* local address and port */
	memset(&sin6_any, 0, sizeof(sin6_any));
	sin6_any.sin6_family = AF_INET6;
	sin6_any.sin6_addr = in6addr_any;
	sin6_any.sin6_port = htons(wfb_options.mc_port);
#ifndef __linux__
	sin6_any.sin6_len = sizeof(struct sockaddr_in6);
#endif

	/* Multicast interface */
	mc_if = if_nametoindex(ctx->dev);
	if (mc_if == 0) {
		p_err("No such interface %s.\n", ctx->dev);
		goto err;
	}

	/* Multicast address */
	memset(&mc_group, 0, sizeof(mc_group));
	mc_group.sin6_family = AF_INET6;
	r = inet_pton(AF_INET6, wfb_options.mc_addr, &mc_group.sin6_addr);
	if (r == 0) {
		p_err("invalid address %s\n", wfb_options.mc_addr);
		goto err;
	}
	else if (r < 0) {
		p_err("inet_pton() failed: %s.\n", strerror(errno));
		goto err;

	}
	if (IN6_IS_ADDR_MC_LINKLOCAL(&mc_group.sin6_addr)) {
		mc_group.sin6_scope_id = mc_if;
	}
	else {
		mc_group.sin6_scope_id = 0;
	}
	mc_group.sin6_port = htons(wfb_options.mc_port);
#ifndef __linux__
	mc_group.sin6_len = sizeof(struct sockaddr_in6);
#endif

	/* configure local address */
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
		p_err("setsockopt(SO_REUSEPORT) failed: %s.\n", strerror(errno));
	}
	if (bind(s, (struct sockaddr *)&sin6_any, sizeof(sin6_any)) < 0) {
		p_err("bind() failed: %s.\n", strerror(errno));
		goto err;
	}

	/* configure multicast rx */
	mreq.ipv6mr_multiaddr = mc_group.sin6_addr;
	mreq.ipv6mr_interface = mc_if;
	if (setsockopt(s, IPPROTO_IPV6,
	    IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
		p_err("setsockopt(IPV6_JOIN_GROUP) failed: %s.\n",
		    strerror(errno));
		goto err;
	}

	/* register */
	ctx->rx_sock = s;
	ctx->rx_ev = netcore_rx_event_add(ctx->net_ctx, ctx->rx_sock, netinet6_rx, ctx);
	if (ctx->rx_ev == NULL) {
		p_err("Cannot register inet6 event.\n");
		goto err;
	}

	return ctx->rx_sock;

err:
	if (s >= 0)
		close(s);
	ctx->rx_sock = -1;
	ctx->rx_ev = NULL;
	return -1;
}

int
netinet6_rx_initialize(struct netinet6_rx_context *ctx,
    struct netcore_context *net_ctx,
    struct rx_context *rx_ctx,
    const char *dev)
{
	assert(ctx);
	assert(net_ctx);
	assert(rx_ctx);
	assert(dev);

	memset(ctx, 0, sizeof(*ctx));
	ctx->net_ctx = net_ctx;
	ctx->rx_ctx = rx_ctx;
	ctx->dev = dev;
	ctx->rx_sock = -1;

	netcore_reload_hook_add(net_ctx, netinet6_rx_socket_open, ctx);

	return netinet6_rx_socket_open(ctx);
}

static int
netinet6_tx_socket_open(void *arg)
{
	struct netinet6_tx_context *ctx = (struct netinet6_tx_context *)arg;
	struct sockaddr_in6 mc_group;
	struct sockaddr_in6 sin6_src;
	struct ifaddrs *ifa, *ifap;
	const int disable = 0;
	const int enable = 1;
	int mc_if;
	int s, r;

	assert(ctx);

	if (ctx->tx_sock >= 0) {
		close(ctx->tx_sock);
		ctx->tx_sock = -1; 
	}

#ifdef SOCK_CLOEXEC
	s = socket(AF_INET6, SOCK_DGRAM|SOCK_CLOEXEC, 0);
#else
	s = socket(AF_INET6, SOCK_DGRAM, 0);
#endif
	if (s < 0) {
		p_err("Socket() failed: %s\n", strerror(errno));
		return -1;
	}

	/* remote address and port */
	mc_if = if_nametoindex(ctx->dev);
	if (mc_if == 0) {
		p_err("No such interface %s.\n", ctx->dev);
		goto err;
	}

	memset(&mc_group, 0, sizeof(mc_group));
	mc_group.sin6_family = AF_INET6;
	mc_group.sin6_port = htons(wfb_options.mc_port);
	mc_group.sin6_flowinfo = 0;
	r = inet_pton(AF_INET6, wfb_options.mc_addr, &mc_group.sin6_addr);
	if (r == 0) {
		p_err("invalid address %s\n", wfb_options.mc_addr);
		goto err;
	}
	else if (r < 0) {
		p_err("inet_pton() failed: %s.\n", strerror(errno));
		goto err;

	}
	if (IN6_IS_ADDR_MC_LINKLOCAL(&mc_group.sin6_addr)) {
		mc_group.sin6_scope_id = mc_if;
	}
	else {
		mc_group.sin6_scope_id = 0;
	}
						  
	/* local address and port */
	sin6_src.sin6_family = AF_UNSPEC;
	if (getifaddrs(&ifa) < 0) {
		p_err("getifaddrs() failed: %s.\n", strerror(errno));
		goto err;
	}
	for (ifap = ifa; ifap; ifap = ifap->ifa_next) {
		struct sockaddr_in6 *ifa6;

		if (ifap->ifa_addr == NULL)
			continue;
		if (ifap->ifa_addr->sa_family != AF_INET6)
			continue;
		ifa6 = (struct sockaddr_in6 *)ifap->ifa_addr;
		if (ifa6->sin6_scope_id != mc_if)
			continue;
		memcpy(&sin6_src, ifa6, sizeof(sin6_src));
		break;
	}
	freeifaddrs(ifa);
	if (sin6_src.sin6_family != AF_INET6) {
		p_err("cannot find valid inet6 address.\n");
		goto err;
	}
	sin6_src.sin6_port = htons(wfb_options.mc_port);
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
		p_err("setsockopt(SO_REUSEPORT) failed: %s.\n", strerror(errno));
	}
	if (bind(s, (struct sockaddr *)&sin6_src, sizeof(sin6_src)) < 0) {
		p_err("bind() failed: %s.\n", strerror(errno));
		goto err;
	}

	/* remote address and port */
	if (connect(s, (struct sockaddr *)&mc_group, sizeof(mc_group)) < 0) {
		p_err("connect() failed: %s.\n", strerror(errno));
		goto err;
	}

	/* configure multicast tx */
	if (setsockopt(s, IPPROTO_IPV6,
	    IPV6_MULTICAST_IF, &mc_if, sizeof(mc_if)) < 0) {
		p_err("setsockopt(IPV6_MULTICAST_IF) failed: %s.\n",
		    strerror(errno));
		goto err;
	}
	if (setsockopt(s, IPPROTO_IPV6,
	    IPV6_MULTICAST_LOOP, &disable, sizeof(disable)) < 0) {
		p_err("setsockopt(IPV6_MULTICAST_LOOP) failed: %s.\n",
		    strerror(errno));
		goto err;
	}

	ctx->tx_sock = s;
	return ctx->tx_sock;
err:
	if (s >= 0)
		close(s);
	ctx->tx_sock = -1;

	return -1;
}

int
netinet6_tx_initialize(struct netinet6_tx_context *ctx,
    struct netcore_context *net_ctx, const char *dev)
{
	assert(ctx);
	assert(dev);

	memset(ctx, 0, sizeof(*ctx));
	ctx->net_ctx = net_ctx;
	ctx->dev = dev;
	ctx->tx_sock = -1;

	netcore_reload_hook_add(net_ctx, netinet6_tx_socket_open, ctx);

	return netinet6_tx_socket_open(ctx);
}

void
netinet6_rx_deinitialize(struct netinet6_rx_context *ctx)
{
	assert(ctx);

	if (ctx->rx_ev) {
		netcore_rx_event_del(ctx->net_ctx, ctx->rx_ev);
		ctx->rx_ev = NULL;
	}
	if (ctx->rx_sock >= 0) {
		close(ctx->rx_sock);
		ctx->rx_sock = -1;
	}
}
