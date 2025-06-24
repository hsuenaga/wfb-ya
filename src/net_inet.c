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
#include "util_inet.h"
#include "util_msg.h"

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

	s = inet_rx_socket(wfb_options.mc_addr, wfb_options.mc_port, ctx->dev,
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
	struct sockaddr_storage ss;
	socklen_t ss_len = sizeof(ss);
	int s;

	assert(ctx);

	if (ctx->tx_sock >= 0) {
		close(ctx->tx_sock);
		ctx->tx_sock = -1; 
	}

	s = inet_tx_socket(wfb_options.mc_addr, wfb_options.mc_port, ctx->dev,
	    (struct sockaddr *)&ss, &ss_len);
	if (s < 0) {
		p_err("inet_tx_socket() failed.\n");
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
