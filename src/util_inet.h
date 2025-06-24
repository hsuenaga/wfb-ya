#ifndef __UTIL_INET_H__
#define __UTIL_INET_H__
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>

extern int inet_rx_socket(const char *s_addr, const char *s_port,
    const char *s_dev, struct sockaddr *sa, socklen_t *sa_len);

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

#endif /* __UTIL_INET_H__ */
