#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "wfb_params.h"
#include "util_msg.h"

struct msg_hook_def {
	int (*func)(void *arg, enum msg_hook_type, const char *, va_list);
	void *arg;
} msg_hook = { NULL, NULL };

static void
invoke_hook(enum msg_hook_type type, const char *fmt, va_list ap)
{
	if (msg_hook.func == NULL)
		return;

	msg_hook.func(msg_hook.arg, type, fmt, ap);
}

__attribute__((format(printf, 1, 2)))
void
__p_info(const char *fmt, ...)
{
	va_list ap;

	assert(fmt);

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	invoke_hook(MSG_TYPE_INFO, fmt, ap);
	va_end(ap);
}

__attribute__((format(printf, 1, 2)))
void
__p_err(const char *fmt, ...)
{
	va_list ap;

	assert(fmt);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	invoke_hook(MSG_TYPE_ERR, fmt, ap);
	va_end(ap);
}

__attribute__((format(printf, 1, 2)))
void
__p_debug(const char *fmt, ...)
{
	va_list ap;

	if (!wfb_options.debug)
		return;

	assert(fmt);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	invoke_hook(MSG_TYPE_DEBUG, fmt, ap);
	va_end(ap);
}

void
msg_set_hook(int(*func)(void *, enum msg_hook_type, const char *, va_list), void *arg)
{
	msg_hook.func = func;
	msg_hook.arg = arg;
}
