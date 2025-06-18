#ifndef __NET_CORE_H__
#define __NET_CORE_H__
#include <stdbool.h>
#include <pthread.h>
#include <event2/event.h>
#include <sys/queue.h>

struct netcore_reload_hook {
	int (*func)(void *);
	void *arg;

	LIST_ENTRY(netcore_reload_hook) next;
};

struct netcore_context {
	pthread_mutex_t lock;
	pthread_t tid;
	struct event *wdt;
	struct event *sighup;
	struct event *sigint;
	struct event *sigterm;
	struct timeval wdt_to;
	bool reload;
	bool cancel;
	bool stopped;

	LIST_HEAD(netcore_rh, netcore_reload_hook) reload_hooks;

	struct event_base *base;
};

extern int netcore_initialize(struct netcore_context *ctx);
extern void netcore_deinitialize(struct netcore_context *ctx);

extern struct event *netcore_rx_event_add(struct netcore_context *ctx, int fd,
    void (*func)(evutil_socket_t, short, void *), void *arg);
extern void netcore_rx_event_del(struct netcore_context *ctx, struct event *ev);
extern int netcore_reload_hook_add(struct netcore_context *ctx,
    int (*func)(void *arg), void *arg);
extern void netcore_reload(struct netcore_context *ctx);
extern void netcore_exit(struct netcore_context *ctx);

extern int netcore_thread_start(struct netcore_context *ctx);
extern void netcore_thread_join(struct netcore_context *ctx);
extern void netcore_thread_cancel(struct netcore_context *ctx);

#endif /* __NET_CORE_H__ */
