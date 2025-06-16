#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>

#include "log_message.h"
#include "log_raw.h"

int
dump_message(FILE *fp, struct log_store *ls)
{
	struct log_data_kv *kv;
	struct log_data_v *v;

	if (fp == NULL)
		fp = stdout;

	TAILQ_FOREACH(kv, &ls->msg_kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, msg_chain) {
			if (v->size == 0)
				continue;
			fprintf(fp, "%ld.%09ld: ", v->ts.tv_sec, v->ts.tv_nsec);
			fwrite(v->buf, v->size, 1, fp);
			fflush(fp);
		}
	}
	return 0;
}
