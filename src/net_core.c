#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include <pthread.h>

#include <sys/queue.h>

#include <event2/event.h>

#include "net_core.h"
#include "util_msg.h"
#include "wfb_params.h"

static void
netcore_term(evutil_socket_t s, short what, void *arg)
{
	struct netcore_context *ctx = (struct netcore_context *)arg;

	assert(ctx);
	p_info("Stopping services...\n");
	event_base_loopexit(ctx->base, NULL);
}

static void
netcore_hup(evutil_socket_t s, short what, void *arg)
{
	struct netcore_context *ctx = (struct netcore_context *)arg;

	assert(ctx);

	wfb_stats.sighup++;
	ctx->reload = true;
}

static void
netcore_wdt(evutil_socket_t s, short what, void *arg)
{
	struct netcore_context *ctx = (struct netcore_context *)arg;
	struct netcore_reload_hook *hook;
	bool error = false;

	assert(ctx);

	if (ctx->reload) {
		LIST_FOREACH(hook, &ctx->reload_hooks, next) {
			if (hook->func(hook->arg) < 0)
				error = true;
		}
		wfb_stats.reload++;
	}

	pthread_mutex_lock(&ctx->lock);
	ctx->reload = error;
	pthread_mutex_unlock(&ctx->lock);
	evtimer_add(ctx->wdt, &ctx->wdt_to);
}

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
	ctx->reload = false;
	LIST_INIT(&ctx->reload_hooks);
	ctx->wdt = evtimer_new(ctx->base, netcore_wdt, ctx);
	if (ctx->wdt == NULL) {
		p_err("Failed to allocate wdt event.\n");
		return -1;
	}
	ctx->wdt_to.tv_sec = 1;
	ctx->wdt_to.tv_usec = 0;
	evtimer_add(ctx->wdt, &ctx->wdt_to);

	ctx->sighup = evsignal_new(ctx->base, SIGHUP, netcore_hup, ctx);
	if (ctx->sighup == NULL) {
		p_err("Failed to allocate signal event.\n");
		return -1;
	}
	evsignal_add(ctx->sighup, NULL);

	ctx->sigterm = evsignal_new(ctx->base, SIGTERM, netcore_term, ctx);
	if (ctx->sigterm == NULL) {
		p_err("Failed to allocate signal event.\n");
		return -1;
	}
	evsignal_add(ctx->sigterm, NULL);

	ctx->sigint = evsignal_new(ctx->base, SIGINT, netcore_term, ctx);
	if (ctx->sigint == NULL) {
		p_err("Failed to allocate signal event.\n");
		return -1;
	}
	evsignal_add(ctx->sigint, NULL);

	event_config_free(cfg);

	return 0;
}

void
netcore_deinitialize(struct netcore_context *ctx)
{
	assert(ctx);

	netcore_thread_cancel(ctx);

	// netcore thread is terminated and joined here.

	if (ctx->wdt) {
		event_del(ctx->wdt);
		event_free(ctx->wdt);
		ctx->wdt = NULL;
	}
	if (ctx->sighup) {
		event_del(ctx->sighup);
		event_free(ctx->sighup);
		ctx->sighup = NULL;
	}
	if (ctx->sigint) {
		event_del(ctx->sigint);
		event_free(ctx->sigint);
		ctx->sigint = NULL;
	}
	if (ctx->sigterm) {
		event_del(ctx->sigterm);
		event_free(ctx->sigterm);
		ctx->sigterm = NULL;
	}
	if (ctx->base) {
		event_base_free(ctx->base);
		ctx->base = NULL;
	}
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
	if (ctx->base) {
		(void)event_del(ev);
	}
	pthread_mutex_unlock(&ctx->lock);

	event_free(ev);
}

extern int
netcore_reload_hook_add(struct netcore_context *ctx,
    int (*func)(void *arg), void *arg)
{
	struct netcore_reload_hook *hook;

	assert(ctx);
	assert(func);

	hook = (struct netcore_reload_hook *)malloc(sizeof(*hook));
	if (hook == NULL) {
		p_err("cannot allocate memory.\n");
		return -1;
	}
	hook->func = func;
	hook->arg = arg;

	LIST_INSERT_HEAD(&ctx->reload_hooks, hook, next);

	return 0;
}

extern void
netcore_reload(struct netcore_context *ctx)
{
	pthread_mutex_lock(&ctx->lock);
	ctx->reload = true;
	pthread_mutex_unlock(&ctx->lock);
}

extern void
netcore_exit(struct netcore_context *ctx)
{
	p_info("Stopping services...\n");
	event_base_loopexit(ctx->base, NULL);
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
	pthread_mutex_lock(&ctx->lock);
	ctx->cancel = true;
	pthread_mutex_unlock(&ctx->lock);

	pthread_cleanup_pop(1);

	pthread_exit(arg);

	// not reached
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
	void *ret = NULL;
	assert(ctx);

	pthread_mutex_lock(&ctx->lock);
	if (!ctx->stopped) {
		pthread_mutex_unlock(&ctx->lock);
		(void)pthread_join(ctx->tid, &ret);
		pthread_mutex_lock(&ctx->lock);
		ctx->stopped = true;
		if (ret == PTHREAD_CANCELED) {
			p_info("netcore thread is canceled.\n");
		}
	}
	pthread_mutex_unlock(&ctx->lock);
}

extern void
netcore_thread_cancel(struct netcore_context *ctx)
{
	assert(ctx);

	pthread_mutex_lock(&ctx->lock);
	if (!ctx->cancel) {
		pthread_mutex_unlock(&ctx->lock);
		if (pthread_cancel(ctx->tid) != 0)
			return;
		pthread_mutex_lock(&ctx->lock);
		ctx->cancel = true;
	}
	pthread_mutex_unlock(&ctx->lock);

	return netcore_thread_join(ctx);
}
