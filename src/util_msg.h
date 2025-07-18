#ifndef __UTIL_LOG_H__
#define __UTIL_LOG_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "util_attribute.h"

enum msg_hook_type {
	MSG_TYPE_NONE,
	MSG_TYPE_INFO,
	MSG_TYPE_ERR,
	MSG_TYPE_DEBUG,
};

extern void __p_info(const char *fmt, ...) __printf;
extern void __p_err(const char *fmt, ...) __printf;
extern void __p_debug(const char *fmt, ...) __printf;
extern void msg_set_hook(
    int(*func)(void *, enum msg_hook_type, const char *, va_list),
    void *arg);
extern int debug;

#define p_info(fmt, ...) do { \
	__p_info(fmt, ##__VA_ARGS__); \
} while (0)

#define p_err(fmt, ...) do { \
	__p_err("%s: ", __func__); \
	__p_err(fmt, ##__VA_ARGS__); \
} while (0)

#define p_debug(fmt, ...) do { \
	__p_debug("--Debug-- %s: ", __func__); \
	__p_debug(fmt, ##__VA_ARGS__); \
} while (0)

static inline const char *
s_mac48(uint8_t *m)
{
	static char s[sizeof("11-22-33-44-55-66")];

	snprintf(s, sizeof(s), "%02x-%02x-%02x-%02x-%02x-%02x",
	   m[0], m[1], m[2], m[3], m[4], m[5]);

	return (const char *)s;
}

static inline const char *
s_binary(uint8_t *b, size_t len)
{
	static char s[BUFSIZ];
	char *sp = s;
	size_t slen = BUFSIZ;
	int i;

	if ((len * 2) > slen) {
		snprintf(sp, slen, "(Binary too long: %zu bytes)", len);
		return (const char *)s;
	}

	for (i = 0; i < len; i++) {
		int n;

		n = snprintf(sp, slen, "%02x", b[i]);
		if (n < 0 || n > slen)
			break;
		sp += n;
		slen -= n;
	}

	return (const char *)s;
}

static inline const char *
s_sockaddr0(struct sockaddr *sa, socklen_t sa_len, bool use_port)
{
	static char s[BUFSIZ];
	char host[NI_MAXHOST], port[NI_MAXSERV];
	int r;

	if (sa == NULL)
		return "(null)";

	r = getnameinfo(sa, sa_len, host, sizeof(host), port, sizeof(port),
	    NI_NUMERICHOST | NI_NUMERICSERV);
	if (r != 0) {
		p_err("getnameinfo() failed: %s.\n", gai_strerror(r));
		return "(error)";
	}

	if (use_port) {
		snprintf(s, sizeof(s), "%s port %s", host, port);
	}
	else {
		snprintf(s, sizeof(s), "%s", host);
	}

	return s;
}

static inline const char *
s_sockaddr(struct sockaddr *sa, socklen_t sa_len)
{
	return s_sockaddr0(sa, sa_len, true);
}


static inline const char *
s_sockaddr_wop(struct sockaddr *sa, socklen_t sa_len)
{
	return s_sockaddr0(sa, sa_len, false);
}
#endif /* __UTIL_LOG_H__ */
