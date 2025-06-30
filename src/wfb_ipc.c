#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#include <event2/event.h>

#include "wfb_params.h"
#include "util_msg.h"

#include "wfb_ipc.h"

static const char *last_path = NULL;

static int
ipc_dump_stat(struct wfb_statistics *st)
{
	assert(st);

	p_info("Statistics:\n");
	p_info("pcap library errors: %" PRIu64 "\n",
	    st->pcap_libpcap_frame_error);
	p_info("pcap radiotap errors: %" PRIu64 "\n",
	    st->pcap_radiotap_frame_error);
	p_info("pcap bad FCS: %" PRIu64 "\n",
	    st->pcap_bad_fcs);
	p_info("pcap invalid channel ID: %" PRIu64 "\n",
	    st->pcap_invalid_channel_id);
	p_info("pcap WFB errors: %" PRIu64 "\n",
	    st->pcap_wfb_frame_error);
	p_info("pcap received packets: %" PRIu64 "\n",
	    st->pcap_accept);
	p_info("Mulitcast UDP header errors: %" PRIu64 "\n",
	    st->mc_udp_frame_error);
	p_info("Mulitcast UDP for corrupted frames: %" PRIu64 "\n",
	    st->mc_udp_corrupted_frames);
	p_info("Multicast UDP WFB errors: %" PRIu64 "\n",
	    st->mc_udp_wfb_frame_error);
	p_info("Multicast UDP received packets: %" PRIu64 "\n",
	    st->mc_accept);

	p_info("IPC success: %" PRIu64 "\n",
	    st->ipc_success);
	p_info("IPC error: %" PRIu64 "\n",
	    st->ipc_error);

	p_info("Frames mirrored: %" PRIu64 "\n",
	    st->mirrored_frames);
	p_info("Frames decoded: %" PRIu64 "\n",
	    st->decoded_frames);

	p_info("Reload: %" PRIu64 "\n",
	    st->reload);
	p_info("signal HUP: %" PRIu64 "\n",
	    st->sighup);

	return 0;
}

static int
ipc_dump(struct ipc_msg *msg)
{
	assert(msg);

	if (msg->result == WFB_IPC_ERR) {
		p_info("Message: %s.\n", msg->u.string);
		return 0;

	}
	if (msg->result != WFB_IPC_OK) {
		p_info("Unknown IPC result(%u) received.\n", msg->result);
		return -1;
	}

	switch (msg->query) {
		case WFB_IPC_PING:
		case WFB_IPC_EXIT:
			return 0;
		case WFB_IPC_STAT:
			return ipc_dump_stat(&msg->u.stat);
		case WFB_IPC_FEC_GET:
		case WFB_IPC_FEC_SET:
			/* fallthrough */
		case WFB_IPC_FEC_TOGGLE:
			p_info("FEC is %s.\n",
				msg->u.value_b ? "disabled" : "enabled");
			return 0;
		default:
			break;
	}

	p_info("Unknown IPC query(%u) received.\n", msg->query);
	return 0;
}

static void
ipc_cleanup(void)
{
	if (last_path)
		(void)unlink(last_path);
}

static ssize_t
ipc_recv_msg(int s, struct ipc_msg *msg)
{
	struct timeval timeout;
	fd_set rfds;
	uint8_t *msgp;
	size_t len;
	ssize_t n;
	int r;

	assert(s >= 0);
	assert(msg);

	FD_ZERO(&rfds);
	FD_SET(s, &rfds);

	msgp = (uint8_t *)msg;
	len = sizeof(*msg);

	while (len > 0) {
		timeout.tv_sec = 0;
		timeout.tv_usec = 100 * 1000; /* 100 [ms] */
		r = select(s + 1, &rfds, NULL, NULL, &timeout);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			p_err("select() failed: %s.\n", strerror(errno));
			return -1;
		}
		if (r == 0) {
			p_info("IPC timeout.\n");
			return -1;
		}

		n = recv(s, msgp, len, 0);
		if (n < 0) {
			p_err("recv(MSG_PEEK) failed: %s.\n", strerror(errno));
			return -1;
		}
		else if (n == 0) {
			/* EOF */
			return 0;
		}
		msgp += n;
		len -= n;
	}

	return sizeof(*msg);
}

static ssize_t
ipc_send_msg(int s, struct ipc_msg *msg)
{
	struct timeval timeout;
	fd_set wfds;
	uint8_t *msgp;
	size_t len;
	ssize_t n;
	int r;

	assert(s >= 0);
	assert(msg);

	msgp = (uint8_t *)msg;
	len = sizeof(*msg);

	FD_ZERO(&wfds);
	FD_SET(s, &wfds);

	while (len > 0) {
		timeout.tv_sec = 0;
		timeout.tv_usec = 100 * 1000; /* 100 [ms] */
		r = select(s + 1, NULL, &wfds, NULL, &timeout);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			p_err("select() failed: %s.\n", strerror(errno));
			return -1;
		}
		if (r == 0) {
			p_info("IPC timeout.\n");
			return -1;
		}

		n = send(s, msgp, len, 0);
		if (n < 0) {
			p_err("send() failed: %s.\n", strerror(errno));
			return -1;
		}
		msgp += n;
		len -= n;
	}

	return len;
}

static int
ipc_get_addr(struct sockaddr_un *sun, const char *path)
{
	size_t n;

	assert(sun);

	memset(sun, 0, sizeof(*sun));
	sun->sun_family = AF_LOCAL;
#ifndef __linux__
	sun->sun_len = sizeof(sun);
#endif
	n = strlcpy(sun->sun_path, path, sizeof(sun->sun_path));
	if (n >= sizeof(sun->sun_path)) {
		p_err("IPC address too long.\n");
		return -1;
	}

	return 0;
}

static int
ipc_create_socket(void)
{
	int s;

	s = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		p_err("socket() failed: %s.\n", strerror(errno));
		return -1;
	}

	if (fcntl(s, F_SETFD, FD_CLOEXEC) < 0) {
		p_err("fcntl() failed: %s.\n", strerror(errno));
		return -1;
	}

	return s;
}

static int
ipc_rx_socket(const char *path)
{
	struct sockaddr_un sun;
	int s;

	assert(path);

	s = ipc_create_socket();
	if (s < 0)
		return -1;

	if (ipc_get_addr(&sun, path) < 0)
		return -1;

	(void)unlink(path);

	if (bind(s, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
		p_err("bind(%s) failed: %s.\n", path, strerror(errno));
		return -1;
	}

	if (listen(s, 1) < 0) {
		p_err("listen(%s) failed: %s.\n", path, strerror(errno));
		return -1;
	}

	last_path = path;
	if (atexit(ipc_cleanup) < 0) {
		p_err("atexit() failed: %s.\n", strerror(errno));
		return -1;
	}

	return s;
}

int
ipc_rx_initialize(struct ipc_rx_context *ctx,
    struct netcore_context *net_ctx, const char *path)
{
	assert(ctx);
	assert(net_ctx);
	assert(path);

	memset(ctx, 0, sizeof(*ctx));
	ctx->net_ctx = net_ctx;
	ctx->rx_sock = ipc_rx_socket(path);
	if (ctx->rx_sock < 0)
		return -1;
	ctx->rx_ev = netcore_rx_event_add(ctx->net_ctx,
	    ctx->rx_sock, ipc_rx, ctx);
	if (ctx->rx_ev == NULL) {
		p_err("Cannot register ipc event.\n");
		return -1;
	}

	return ctx->rx_sock;
}

int
ipc_tx_socket(const char *path)
{
	struct sockaddr_un sun;
	int s;

	assert(path);

	s = ipc_create_socket();
	if (s < 0)
		return -1;

	if (ipc_get_addr(&sun, path) < 0)
		return -1;

	if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
		p_err("connect(%s) failed: %s.\n", path, strerror(errno));
		return -1;
	}

	return s;
}

static int
ipc_rx_reply(int s, struct ipc_msg *msg, bool is_ok)
{
	ssize_t n;

	assert(s >= 0);
	assert(msg);

	msg->result = is_ok ? WFB_IPC_OK : WFB_IPC_ERR;
	n = ipc_send_msg(s, msg);
	if (n < 0)
		return -1;

	return 0;
}

void
ipc_rx(evutil_socket_t fd, short event, void *arg)
{
	struct ipc_rx_context *ctx = (struct ipc_rx_context *)arg;
	struct netcore_context *net_ctx;
	struct ipc_msg msg;
	ssize_t n;
	int s;

	assert(ctx);
	net_ctx = ctx->net_ctx;

	s = accept(fd, NULL, 0);
	if (s < 0) {
		p_err("accept() failed: %s.\n", strerror(errno));
		wfb_stats.ipc_error++;
		return;
	}
	n = ipc_recv_msg(s, &msg);
	if (n < 0) {
		wfb_stats.ipc_error++;
		close(s);
		return;
	}
	else if (n == 0) {
		wfb_stats.ipc_error++;
		p_info("Connection closed.\n");
		close(s);
		return;
	}
	p_info("IPC Rx: %zd bytes received.\n", n);
	wfb_stats.ipc_success++;

	switch (msg.query) {
		case WFB_IPC_PING:
			p_info("Execute IPC PING.\n");
			ipc_rx_reply(s, &msg, true);
			break;
		case WFB_IPC_STAT:
			p_info("Execute IPC STAT.\n");
			msg.u.stat = wfb_stats;
			ipc_rx_reply(s, &msg, true);
			break;
		case WFB_IPC_EXIT:
			p_info("Execute IPC EXIT.\n");
			ipc_rx_reply(s, &msg, true);
			netcore_exit(net_ctx);
			break;
		case WFB_IPC_FEC_TOGGLE:
			p_info("Execute IPC FEC_TOGGLE.\n");
			wfb_options.no_fec = !wfb_options.no_fec;
			msg.u.value_b = wfb_options.no_fec;
			ipc_rx_reply(s, &msg, true);
			break;
		case WFB_IPC_FEC_GET:
			p_info("Execute IPC FEC_GET.\n");
			msg.u.value_b = wfb_options.no_fec;
			ipc_rx_reply(s, &msg, true);
			break;
		case WFB_IPC_OK:
		case WFB_IPC_ERR:
		default:
			snprintf(msg.u.string, sizeof(msg.u.string),
			    "Invalid IPC query(%u).\n", msg.query);
			ipc_rx_reply(s, &msg, false);
			p_err("Invalid IPC message.\n");
			break;
	}

	close(s);
	return;
}

static int
ipc_tx_recv_response(int s, struct ipc_msg *msg)
{
	ssize_t n;

	assert(s >= 0);

	n = ipc_recv_msg(s, msg);
	if (n < 0) {
		p_err("read() failed: %s.\n", strerror(errno));
		return -1;
	}
	else if (n == 0) {
		p_info("Connection closed.\n");
		return -1;
	}

	return msg->result == WFB_IPC_OK ? 0 : -1;
}

int
ipc_tx(const char *path, const char *param)
{
	struct ipc_msg msg;
	ssize_t n;
	int s;

	memset(&msg, 0, sizeof(msg));

	if (strcasecmp("ping", param) == 0) {
		msg.query = WFB_IPC_PING;
	}
	else if (strcasecmp("stat", param) == 0) {
		msg.query = WFB_IPC_STAT;
	}
	else if (strcasecmp("exit", param) == 0) {
		msg.query = WFB_IPC_EXIT;
	}
	else if (strcasecmp("quit", param) == 0) {
		msg.query = WFB_IPC_EXIT;
	}
	else if (strcasecmp("fec", param) == 0) {
		msg.query = WFB_IPC_FEC_GET;
	}
	else if (strcasecmp("fec_toggle", param) == 0) {
		msg.query = WFB_IPC_FEC_TOGGLE;
	}
	else {
		p_err("Invalid argument: %s.\n", param);
		return -1;
	}

	s = ipc_tx_socket(path);
	if (s < 0)
		return -1;

	n = ipc_send_msg(s, &msg);
	if (n < 0) {
		close(s);
		return -1;
	}

	if (ipc_tx_recv_response(s, &msg) < 0) {
		p_info("IPC failure: %s.\n", msg.u.string);
		close(s);
		return -1;
	}

	p_debug("IPC success.\n");
	ipc_dump(&msg);

	close(s);
	return 0;
}
