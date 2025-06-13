#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../util_attribute.h"
#include "../compat.h"

#include "log_analysis.h"
#include "log_raw.h"
#include "log_csv.h"

static void __fprintf
fprintfq(FILE *fp, const char *fmt, ...)
{
	va_list ap;

	assert(fp);
	assert(fmt);

	va_start(ap, fmt);
	fprintf(fp, "\"");
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\"");
	va_end(ap);
}

static void
add_sep(FILE *fp)
{
	assert(fp);

	fprintf(fp, ",");
}

static void
add_nl(FILE *fp)
{
	assert(fp);

	fprintf(fp, "\n");
}

static int
csv_serialize_v(FILE *fp, struct log_data_kv *kv, struct log_data_v *v)
{
	static const char *s_hdr[] = {
		"Sequence",
		"Time Stamp",
		"Block Index",
		"Fragment Index",
		"Source Node",
		"Frequency",
		"dbm",
		"Data Size",
		"Corrupted"
	};
	char s_addr[INET6_ADDRSTRLEN] = {'\0'};
	int i;

	assert(fp);

	if (kv == NULL || v == NULL) {
		for (i = 0; i < NELEMS(s_hdr); i++) {
			if (i != 0)
				add_sep(fp);
			fprintfq(fp, "%s", s_hdr[i]);
		}
		add_nl(fp);
		return 0;
	}

	if (v->rx_src.sin6_family == AF_INET6) {
		inet_ntop(AF_INET6,
		    &v->rx_src.sin6_addr, s_addr, sizeof(s_addr));
	}

	fprintfq(fp, "%" PRIu64 "", kv->key);
	add_sep(fp);
        fprintfq(fp, "%ld.%09ld", v->ts.tv_sec, v->ts.tv_nsec);
	add_sep(fp);
	fprintfq(fp, "%" PRIu64 "", v->block_idx);
	add_sep(fp);
	fprintfq(fp, "%" PRIu64 "", v->fragment_idx);
	add_sep(fp);
	fprintfq(fp, "%s", s_addr);
	add_sep(fp);
	if (v->freq > 0)
		fprintfq(fp, "%u", v->freq);
	add_sep(fp);
	if (v->dbm >= INT8_MIN && v->dbm <= INT8_MAX)
		fprintfq(fp, "%d", v->dbm);
	add_sep(fp);
	fprintfq(fp, "%u", v->size);
	add_sep(fp);
	fprintfq(fp, "%d", v->corrupt ? 1 : 0);
	add_nl(fp);


	return 0;
}

int
csv_serialize(FILE *fp, struct log_store *ls)
{
	struct log_data_kv *kv;
	struct log_data_v *v;

	if (fp == NULL)
		fp = stdout;

	csv_serialize_v(fp, NULL, NULL);
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			csv_serialize_v(fp, kv, v);
		}
	}

	return 0;
}
