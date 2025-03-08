#ifndef __NET_CORE_H__
#define __NET_CORE_H__
#include <pthread.h>
#include <event2/event.h>

struct netcore_context {
	pthread_mutex_t lock;
	pthread_t tid;

	struct event_base *base;
};

extern int netcore_initialize(struct netcore_context *ctx);
extern void netcore_deinitialize(struct netcore_context *ctx);

extern struct event *netcore_rx_event_add(struct netcore_context *ctx, int fd,
    void (*func)(evutil_socket_t, short, void *), void *arg);
extern void netcore_event_del(struct event *ev);

extern int netcore_thread_start(struct netcore_context *ctx);
extern void netcore_thread_join(struct netcore_context *ctx);
extern void netcore_thread_cancel(struct netcore_context *ctx);

#endif /* __NET_CORE_H__ */
