#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "util_log.h"

__attribute__((format(printf, 1, 2)))
void
__p_info(const char *fmt, ...)
{
	va_list ap;

	assert(fmt);

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
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
	va_end(ap);
}


