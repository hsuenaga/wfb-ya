#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <pthread.h>

#include <sys/param.h>

#include "compat.h"

#ifdef __linux__
static pthread_mutex_t bn_lock = PTHREAD_MUTEX_INITIALIZER;

char *basename_r(const char *path, char *bname)
{
	char bnbuf[MAXPATHLEN];
	char *bn;

	if (!path || !bname)
		return NULL;

	pthread_mutex_lock(&bn_lock);
	strncpy(bnbuf, path, MAXPATHLEN);
	bn = basename(bnbuf);
	strncpy(bname, bn, MAXPATHLEN);
	pthread_mutex_unlock(&bn_lock);

	return bname;
}
#endif
