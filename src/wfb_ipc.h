#ifndef __WFB_IPC_H__
#define __WFB_IPC_H__
#include <event2/event.h>
#include "net_core.h"
#include "wfb_params.h"

#define IPC_MSG_LEN 64

struct ipc_rx_context {
	struct netcore_context *net_ctx;
	struct event *rx_ev;
	int rx_sock;
};

enum ipc_msg_type {
	WFB_IPC_INVAL = 0,
	WFB_IPC_OK = 1,
	WFB_IPC_ERR = 2,
	WFB_IPC_PING = 3,
	WFB_IPC_STAT = 4,
	WFB_IPC_EXIT = 5,
	WFB_IPC_FEC_SET = 6,
	WFB_IPC_FEC_GET = 7,
	WFB_IPC_FEC_TOGGLE = 8,
};

struct ipc_msg {
	uint8_t query;
	uint8_t result;
	uint8_t pad[2 + 4];

	/* align 64bit */
	union {
		char string[IPC_MSG_LEN];
		struct wfb_statistics stat;
		bool value_b;
	} u;
};

extern int ipc_rx_initialize(struct ipc_rx_context *ctx,
    struct netcore_context *net_ctx, const char *path);
extern int ipc_tx_socket(const char *path);
extern void ipc_rx(evutil_socket_t fd, short event, void *arg);
extern int ipc_tx(const char *path, const char *param);
#endif /* __WFB_IPC_H__ */
