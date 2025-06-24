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
#include "net_inet.h"
#include "util_msg.h"

static inline bool
is_addr_multicast(struct sockaddr *sa)
{
	assert(sa);

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		uint32_t addr32 = ntohl(sin->sin_addr.s_addr);

		if (IN_MULTICAST(addr32))
			return true;
	}
	else if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		if (IN6_IS_ADDR_MC_NODELOCAL(&sin6->sin6_addr))
			return true;
		if (IN6_IS_ADDR_MC_LINKLOCAL(&sin6->sin6_addr))
			return true;
		if (IN6_IS_ADDR_MC_SITELOCAL(&sin6->sin6_addr))
			return true;
		if (IN6_IS_ADDR_MC_ORGLOCAL(&sin6->sin6_addr))
			return true;
		if (IN6_IS_ADDR_MC_GLOBAL(&sin6->sin6_addr))
			return true;
	}

	return false;
}

static inline bool
is_addr_linklocal(struct sockaddr *sa)
{
	struct sockaddr_in6 *sin6;

	assert(sa);

	if (sa->sa_family != AF_INET6)
		return false;

	sin6 = (struct sockaddr_in6 *)sa;

	if (IN6_IS_ADDR_MC_LINKLOCAL(&sin6->sin6_addr))
		return true;
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
		return true;

	return false;
}

static int
get_any_address(int af, struct sockaddr *sa, socklen_t *sa_len)
{
	assert(af == AF_INET || af == AF_INET6);
	assert((af == AF_INET && *sa_len >= sizeof(struct sockaddr_in)) ||
	       (af == AF_INET6 && *sa_len >= sizeof(struct sockaddr_in6)));
	assert(sa);

	memset(sa, 0, *sa_len);
	sa->sa_family = af;
	if (af == AF_INET) {
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)sa;
		sin->sin_addr.s_addr = htonl(INADDR_ANY);
		*sa_len = sizeof(struct sockaddr_in);
	}
	else if (af == AF_INET6) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)sa;
		sin6->sin6_addr = in6addr_any;
		*sa_len = sizeof(struct sockaddr_in6);
	}
#ifndef __linux__
	sa->sa_len = *sa_len;
#endif

	return 0;
}

static int
get_local_address(int af,
    struct sockaddr *sa, socklen_t *sa_len, const char *s_dev)
{
	struct ifaddrs *ifa0, *ifa;
	bool found = false;
	int r;

	assert(af == AF_INET || af == AF_INET6);
	assert((af == AF_INET && *sa_len >= sizeof(struct sockaddr_in)) ||
	       (af == AF_INET6 && *sa_len >= sizeof(struct sockaddr_in6)));
	assert(sa);

	if (s_dev) {
		r = getifaddrs(&ifa0);
		if (r != 0) {
			p_err("getifaddrs() failed: %s.\n", strerror(errno));
			return -1;
		}
		for (ifa = ifa0; ifa; ifa = ifa->ifa_next) {
			if (strcmp(ifa->ifa_name, s_dev) != 0)
				continue;
			if (ifa->ifa_addr->sa_family != af)
				continue;
			if (af == AF_INET) {
				*sa_len = sizeof(struct sockaddr_in);
				memcpy(sa, ifa->ifa_addr, *sa_len);
			}
			else if (af == AF_INET6) {
				*sa_len = sizeof(struct sockaddr_in6);
				memcpy(sa, ifa->ifa_addr, *sa_len);
			}
			found = true;
			break;
		}
		freeifaddrs(ifa0);
	}

	if (found)
		return 0;

	return get_any_address(af, sa, sa_len);
}

static int
in6_addr_fixup(struct sockaddr *sa, const char *s_dev)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	int ifidx;

	assert(sa);

	if (sa->sa_family != AF_INET6)
		return 0;

	if (!is_addr_linklocal(sa))
		return 0;

	if (s_dev == NULL && sin6->sin6_scope_id == 0) {
		p_err("IPv6 Link-Local address detected, "
		      "but no scope-id specified.\n");
		return -1;
	}

	ifidx = if_nametoindex(s_dev);
	if (ifidx == 0) {
		p_err("Invalid interface %s.\n", s_dev);
		return -1;
	}

	if (sin6->sin6_scope_id == 0) {
		sin6->sin6_scope_id = ifidx;
		return 0;
	}

	if (sin6->sin6_scope_id != ifidx) {
		p_err("Interface Index mismatch.\n");
		return -1;
	}

	return 0;
}

static int
copy_port(struct sockaddr *dst, struct sockaddr *src)
{
	assert(dst);
	assert(src);

	if (dst->sa_family != src->sa_family) {
		p_err("Address family mismatch.\n");
		return -1;
	}

	if (dst->sa_family == AF_INET) {
		struct sockaddr_in *sin_dst = (struct sockaddr_in *)dst;
		struct sockaddr_in *sin_src = (struct sockaddr_in *)src;

		sin_dst->sin_port = sin_src->sin_port;
	}
	else if (dst->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6_dst = (struct sockaddr_in6 *)dst;
		struct sockaddr_in6 *sin6_src = (struct sockaddr_in6 *)src;

		sin6_dst->sin6_port = sin6_src->sin6_port;
	}

	return 0;
}

static int
bind_unicast(int s, struct sockaddr *sa, socklen_t sa_len, const char *s_dev)
{
	const int enable = 1;
	int r;

	assert(s >= 0);
	assert(sa);

	r = setsockopt(s, SOL_SOCKET, SO_REUSEPORT,
	    &enable, sizeof(enable));
	if (r < 0) {
		p_err("setsockopt(SO_RESUSEPORT) failed: %s\n",
		    strerror(errno));
		return -1;
	}

	r = setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	    &enable, sizeof(enable));
	if (r < 0) {
		p_err("setsockopt(SO_REUSEADDR) failed: %s\n",
		    strerror(errno));
		return -1;
	}

	r = in6_addr_fixup(sa, s_dev);
	if (r < 0) {
		p_err("Cannot create link-local address.\n");
		return -1;
	}

	r = bind(s, sa, sa_len);
	if (r < 0) {
		p_err("bind() failed: %s\n", strerror(errno));
		return -1;
	}

	p_info("Listening on %s.\n", s_sockaddr(sa, sa_len));
	p_info("Unicast Rx.\n");

	return 0;
}

static int
bind_multicast(int s, struct sockaddr *sa, socklen_t sa_len, const char *s_dev)
{
	struct sockaddr_storage ss;
	socklen_t ss_len = sizeof(ss);
	const int enable = 1;
	int r;

	assert(sa);

	r = in6_addr_fixup(sa, s_dev);
	if (r < 0) {
		p_err("Invalid IPv6 address.\n");
		return -1;
	}

	r = get_local_address(sa->sa_family,
	    (struct sockaddr *)&ss, &ss_len, s_dev);
	if (r < 0) {
		p_err("Cannot select local address.\n");
		return -1;
	}

	r = copy_port((struct sockaddr *)&ss, sa);
	if (r < 0) {
		p_err("Cannot extract port info.\n");
		p_err("ss => %s\n", s_sockaddr((struct sockaddr *)&ss, ss_len));
		p_err("sa => %s\n", s_sockaddr(sa, sa_len));
		return -1;
	}

	r = setsockopt(s,
	    SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
	if (r < 0) {
		p_err("setsockopt(SO_REUSEPORT) failed: %s.\n",
		    strerror(errno));
		return -1;
	}

	r = setsockopt(s,
	    SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
	if (r < 0) {
		p_err("setsockopt(SO_REUSEADDR) failed: %s.\n",
		    strerror(errno));
		return -1;
	}

	r = bind(s, (struct sockaddr *)&ss, ss_len);
	if (r < 0) {
		p_err("bind() failed: %s.\n", strerror(errno));
		return -1;
	}

	p_info("Listening on %s.\n",
	    s_sockaddr((struct sockaddr *)&ss, ss_len));
		
	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin_if = (struct sockaddr_in *)&ss;
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		struct ip_mreq mreq;

		memset(&mreq,0, sizeof(mreq));
		mreq.imr_interface.s_addr = sin_if->sin_addr.s_addr;
		mreq.imr_multiaddr.s_addr = sin->sin_addr.s_addr;

		r = setsockopt(s,
		    IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
		if (r < 0) {
			p_err("setsockopt(IP_ADD_MEMBERSHIP) failed: %s.\n",
			    strerror(errno));
			return -1;
		}
	}
	else if (sa->sa_family == AF_INET6) {
		unsigned int ifidx = 0;
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
		struct ipv6_mreq mreq;

		if (s_dev)
			ifidx = if_nametoindex(s_dev);

		memset(&mreq, 0, sizeof(mreq));
		mreq.ipv6mr_multiaddr = sin6->sin6_addr;
		mreq.ipv6mr_interface = ifidx;

		r = setsockopt(s,
		    IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
		if (r < 0) {
			p_err("setsockopt(IPV6_JOIN_GROUP) failed: %s.\n",
			   strerror(errno));
			return -1;
		}
	}

	p_info("Multicast Rx. Group is %s.\n", s_sockaddr_wop(sa, sa_len));

	return 0;
}

static int
rx_socket_open(const char *s_addr, const char *s_port, const char *s_dev,
    struct sockaddr *sa, socklen_t *sa_len)
{
	struct addrinfo *res, *ai;
	struct addrinfo hints;
	int s = -1;
	int r;

	assert(s_addr);
	assert(s_port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
#ifdef SOCK_CLOEXEC
	hints.ai_socktype = SOCK_DGRAM | SOCK_CLOEXEC;
#else
	hints.ai_socktype = SOCK_DGRAM;
#endif
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_PASSIVE;
	if (!wfb_options.use_dns) {
		hints.ai_flags |= (AI_NUMERICHOST | AI_NUMERICSERV);
	}

	r = getaddrinfo(s_addr, s_port, &hints, &res);
	if (r != 0) {
		p_err("getaddrinfo() failed: %s\n", gai_strerror(r));
		return -1;
	}
	for (ai = res; ai; ai = ai->ai_next) {
		s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s < 0) {
			p_err("socket() failed: %s\n", strerror(errno));
			freeaddrinfo(res);
			return -1;
		}

		if (is_addr_multicast(ai->ai_addr)) {
			r = bind_multicast(s,
			    ai->ai_addr, ai->ai_addrlen, s_dev);
			if (r < 0) {
				p_err("bind_multicast failed.\n");
				return -1;
			}
		}
		else {
			r = bind_unicast(s,
			    ai->ai_addr, ai->ai_addrlen, s_dev);
			if (r < 0) {
				p_err("bind_unicast failed.\n");
				return -1;
			}
		}

		if (sa && *sa_len >= ai->ai_addrlen) {
			*sa_len = ai->ai_addrlen;
			memcpy(sa, ai->ai_addr, *sa_len);
		}
		break;
	}
	freeaddrinfo(res);

	if (s < 0) {
		p_err("Could not find valid address for %s:%s.\n",
		    s_addr, s_port);
		return -1;
	}

	return s;
}

static void
netinet_rx(evutil_socket_t fd, short event, void *arg)
{
	struct netinet_rx_context *ctx = (struct netinet_rx_context *)arg;
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
netinet_tx(struct iovec *iov, int iovcnt, void *arg)
{
	struct netinet_tx_context *ctx = (struct netinet_tx_context *)arg;
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
netinet_rx_socket_open(void *arg)
{
	struct netinet_rx_context *ctx = (struct netinet_rx_context *)arg;
	struct sockaddr_storage ss;
	socklen_t ss_len = sizeof(ss);
	int s;

	assert(ctx);

	if (ctx->rx_ev) {
		netcore_rx_event_del(ctx->net_ctx, ctx->rx_ev);
		ctx->rx_ev = NULL;
	}
	if (ctx->rx_sock >= 0) {
		close(ctx->rx_sock);
		ctx->rx_sock = -1;
	}

	s = rx_socket_open(wfb_options.mc_addr, wfb_options.mc_port, ctx->dev,
	    (struct sockaddr *)&ss, &ss_len);
	if (s < 0) {
		p_err("failed to create socket.\n");
		return -1;
	}

	ctx->rx_sock = s;
	ctx->rx_ev = netcore_rx_event_add(ctx->net_ctx, ctx->rx_sock,
	    netinet_rx, ctx);
	if (ctx->rx_ev == NULL) {
		p_err("Cannot register inet event.\n");
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
netinet_rx_initialize(struct netinet_rx_context *ctx,
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

	netcore_reload_hook_add(net_ctx, netinet_rx_socket_open, ctx);

	return netinet_rx_socket_open(ctx);
}

static int
netinet_tx_socket_open(void *arg)
{
	struct netinet_tx_context *ctx = (struct netinet_tx_context *)arg;
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
	mc_group.sin6_port = atoi(wfb_options.mc_port);
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
		p_err("cannot find valid inet address.\n");
		goto err;
	}
	sin6_src.sin6_port = atoi(wfb_options.mc_port);
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
netinet_tx_initialize(struct netinet_tx_context *ctx,
    struct netcore_context *net_ctx, const char *dev)
{
	assert(ctx);
	assert(dev);

	memset(ctx, 0, sizeof(*ctx));
	ctx->net_ctx = net_ctx;
	ctx->dev = dev;
	ctx->tx_sock = -1;

	netcore_reload_hook_add(net_ctx, netinet_tx_socket_open, ctx);

	return netinet_tx_socket_open(ctx);
}

void
netinet_rx_deinitialize(struct netinet_rx_context *ctx)
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
