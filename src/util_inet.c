#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <ifaddrs.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>

#include "wfb_params.h"
#include "util_inet.h"
#include "util_msg.h"

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

int
inet_rx_socket(const char *s_addr, const char *s_port, const char *s_dev,
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

