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
	socklen_t sslen;
	ssize_t rxlen;

	assert(ctx);

	sslen = sizeof(ctx->ss_src);
#ifndef __linux__
	ctx->ss_src.ss_len = sslen;
#endif
	ctx->rx_ctx->rx_src.sin6_family = AF_UNSPEC;
retry:
	rxlen = recvfrom(fd, ctx->rxbuf, sizeof(ctx->rxbuf), 0,
	    (struct sockaddr *)&ctx->ss_src, &sslen);
	if (rxlen < 0) {
		if (errno == EINTR) {
			goto retry;
		}
		p_debug("recvfrom() failed: %s.\n", strerror(errno));
		return;
	}

	if (ctx->ss_src.ss_family == AF_INET6) {
		memcpy(&ctx->rx_ctx->rx_src, &ctx->ss_src, sslen);
	}

	rx_frame_udp(ctx->rx_ctx, ctx->rxbuf, rxlen);
}

int
netinet6_rx_initialize(struct netinet6_rx_context *ctx,
    struct netcore_context *net_ctx,
    struct rx_context *rx_ctx,
    const char *dev)
{
	struct sockaddr_in6 sin6;
	struct ipv6_mreq mreq;
	const int enable = 1;
	int s = -1;
	int r;

	assert(ctx);
	assert(net_ctx);
	assert(rx_ctx);
	assert(dev);

	memset(ctx, 0, sizeof(*ctx));
	ctx->net_ctx = net_ctx;
	ctx->rx_ctx = rx_ctx;

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
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = in6addr_any;
	sin6.sin6_port = htobe16(wfb_options.mc_port);
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
		p_err("setsockopt(SO_REUSEPORT) failed: %s.\n", strerror(errno));
	}
	if (bind(s, (struct sockaddr *)&sin6, sizeof(sin6)) < 0) {
		p_err("bind() failed: %s.\n", strerror(errno));
		goto err;
	}

	/* remote address and port */
	ctx->mc_if = if_nametoindex(dev);
	if (ctx->mc_if == 0) {
		p_err("No such interface %s.\n", dev);
		goto err;
	}

	ctx->mc_group.sin6_family = AF_INET6;
	ctx->mc_group.sin6_port = htobe16(wfb_options.mc_port);
	ctx->mc_group.sin6_flowinfo = 0;
	r = inet_pton(AF_INET6, wfb_options.mc_addr, &ctx->mc_group.sin6_addr);
	if (r == 0) {
		p_err("invalid address %s\n", wfb_options.mc_addr);
		goto err;
	}
	else if (r < 0) {
		p_err("inet_pton() failed: %s.\n", strerror(errno));
		goto err;

	}
	ctx->mc_group.sin6_scope_id = ctx->mc_if; // XXX: getaddrinfo()

	/* configure multicast tx/rx */
	mreq.ipv6mr_multiaddr = ctx->mc_group.sin6_addr;
	mreq.ipv6mr_interface = ctx->mc_if;
	if (setsockopt(s, IPPROTO_IPV6,
	    IPV6_JOIN_GROUP, &mreq, sizeof(mreq)) < 0) {
		p_err("setsockopt(IPV6_JOIN_GROUP) failed: %s.\n",
		    strerror(errno));
		goto err;
	}

	ctx->sock = s;
	ctx->ev = netcore_rx_event_add(net_ctx, s, netinet6_rx, ctx);
	if (ctx->ev == NULL) {
		p_err("Cannot register inet6 event.\n");
		goto err;
	}
	return ctx->sock;
err:
	if (s >= 0)
		close(s);

	return -1;
}

int
netinet6_tx_initialize(struct netinet6_tx_context *ctx, const char *dev)
{
	struct sockaddr_in6 sin6;
	struct ifaddrs *ifa, *ifap;
	const int disable = 0;
	const int enable = 1;
	int s = -1;
	int r;

	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));

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
	ctx->mc_if = if_nametoindex(dev);
	if (ctx->mc_if == 0) {
		p_err("No such interface %s.\n", dev);
		goto err;
	}

	ctx->mc_group.sin6_family = AF_INET6;
	ctx->mc_group.sin6_port = htobe16(wfb_options.mc_port);
	ctx->mc_group.sin6_flowinfo = 0;
	r = inet_pton(AF_INET6, wfb_options.mc_addr, &ctx->mc_group.sin6_addr);
	if (r == 0) {
		p_err("invalid address %s\n", wfb_options.mc_addr);
		goto err;
	}
	else if (r < 0) {
		p_err("inet_pton() failed: %s.\n", strerror(errno));
		goto err;

	}
	ctx->mc_group.sin6_scope_id = ctx->mc_if; // XXX: getaddrinfo()
						  
	/* local address and port */
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_UNSPEC;
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
		if (ifa6->sin6_scope_id != ctx->mc_if)
			continue;
		memcpy(&sin6, ifa6, sizeof(sin6));
		break;
	}
	freeifaddrs(ifa);
	if (sin6.sin6_family != AF_INET6) {
		p_err("cannot find valid inet6 address.\n");
		goto err;
	}
	sin6.sin6_port = htobe16(wfb_options.mc_port);
	if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
		p_err("setsockopt(SO_REUSEPORT) failed: %s.\n", strerror(errno));
	}
	if (bind(s, (struct sockaddr *)&sin6, sizeof(sin6)) < 0) {
		p_err("bind() failed: %s.\n", strerror(errno));
		goto err;
	}

	/* remote address and port */
	if (connect(s, (struct sockaddr *)&ctx->mc_group, sizeof(ctx->mc_group)) < 0) {
		p_err("connect() failed: %s.\n", strerror(errno));
		goto err;
	}

	/* configure multicast tx */
	if (setsockopt(s, IPPROTO_IPV6,
	    IPV6_MULTICAST_IF, &ctx->mc_if, sizeof(ctx->mc_if)) < 0) {
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

	ctx->sock = s;
	return ctx->sock;
err:
	if (s >= 0)
		close(s);

	return -1;
}

void
netinet6_rx_deinitialize(struct netinet6_rx_context *ctx)
{
	assert(ctx);

	if (ctx->ev) {
		event_del(ctx->ev);
		event_free(ctx->ev);
		ctx->ev = NULL;
	}
	if (ctx->sock >= 0) {
		close(ctx->sock);
		ctx->sock = -1;
	}
}

void
netinet6_tx(struct iovec *iov, int iovcnt, void *arg)
{
	struct netinet6_tx_context *ctx = (struct netinet6_tx_context *)arg;
	ssize_t n;

	assert(ctx);
	assert(iov);
	assert(iovcnt > 0);
	assert(ctx->sock >= 0);

retry:
	/*
	n = sendto(ctx->sock, data, size, MSG_DONTWAIT,
	    (struct sockaddr *)&ctx->mc_group, sizeof(ctx->mc_group));
        */
	n = writev(ctx->sock, iov, iovcnt);
	if (n < 0) {
		if (errno == EINTR) {
			goto retry;
		}
		p_err("writev() failed: %s\n", strerror(errno));
	}
}
