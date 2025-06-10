#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <pthread.h>

#include <event2/event.h>

#include "net_core.h"
#include "util_msg.h"

int
netcore_initialize(struct netcore_context *ctx)
{
	struct event_config *cfg;

	assert(ctx);

	memset(ctx, 0, sizeof(*ctx));

	cfg = event_config_new();
	if (cfg == NULL) {
		p_err("Failed to create event config.\n");
		return -1;
	}

	ctx->base = event_base_new();
	pthread_mutex_init(&ctx->lock, NULL);

	event_config_free(cfg);

	return 0;
}

void
netcore_deinitialize(struct netcore_context *ctx)
{
	assert(ctx);

	netcore_thread_cancel(ctx);
	event_base_free(ctx->base);
	ctx->base = NULL;
}

struct event *
netcore_rx_event_add(struct netcore_context *ctx, int fd,
    void (*func)(evutil_socket_t, short, void *), void *arg)
{
	struct event *ev;

	assert(ctx);
	assert(ctx->base);
	assert(func);

	ev = event_new(ctx->base, fd, EV_READ|EV_PERSIST, func, arg);
	if (ev == NULL) {
		p_err("Cannot initialize event.\n");
		return NULL;
	}

	pthread_mutex_lock(&ctx->lock);
	(void)event_add(ev, NULL);
	pthread_mutex_unlock(&ctx->lock);

	return ev;
}

extern void
netcore_rx_event_del(struct netcore_context *ctx, struct event *ev)
{
	pthread_mutex_lock(&ctx->lock);
	(void)event_del(ev);
	pthread_mutex_unlock(&ctx->lock);
}

static void
thread_cleanup(void *arg)
{
	struct netcore_context *ctx = (struct netcore_context *)arg;

	(void)pthread_mutex_unlock(&ctx->lock);
}

static void *
thread_main(void *arg)
{
	struct netcore_context *ctx = arg;

	assert(ctx);

	pthread_cleanup_push(thread_cleanup, ctx);

	event_base_dispatch(ctx->base);
	// NOTE: events are not free()'ed yet.

	pthread_cleanup_pop(1);

	return NULL;
}

extern int
netcore_thread_start(struct netcore_context *ctx)
{
	int err;

	assert(ctx);

	err = pthread_create(&ctx->tid, NULL, thread_main, ctx);
	if (err != 0) {
		p_err("pthread_create() failed: %s\n", strerror(err));
		return -1;
	}

	return 0;
}

extern void
netcore_thread_join(struct netcore_context *ctx)
{
	void *ret;

	assert(ctx);

	(void)pthread_join(ctx->tid, &ret);
}

extern void
netcore_thread_cancel(struct netcore_context *ctx)
{
	assert(ctx);

	if (pthread_cancel(ctx->tid) != 0)
		return;

	return netcore_thread_join(ctx);
}
