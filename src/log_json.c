#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "util_attribute.h"
#include "compat.h"

#include "log_analysis.h"
#include "log_raw.h"
#include "log_csv.h"
#include "log_json.h"

const char *json_keys[] = {
	"Sequence",
	"TimeStamp",
	"BlockIndex",
	"FragmentIndex",
	"SourceNode",
	"Frequency",
	"dbm",
	"DataSize",
	"IsFEC",
	"Type",
};

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
vfprintfq(FILE *fp, const char *fmt, va_list ap)
{
	assert(fp);
	assert(fmt);

	fprintf(fp, "\"");
	vfprintf(fp, fmt, ap);
	fprintf(fp, "\"");
}

static void __printflike(3, 4)
fprintfkv(FILE *fp, enum json_keys_enum k, const char *fmt, ...)
{
	va_list ap;

	assert(fp);
	assert(fmt);

	va_start(ap, fmt);
	fprintfq(fp, "%s", json_keys[k]);
	fprintf(fp, ":");
	switch (k) {
		case KEY_SQ:
		case KEY_TS:
		case KEY_BI:
		case KEY_FI:
		case KEY_FQ:
		case KEY_DB:
		case KEY_DS:
			vfprintf(fp, fmt, ap);
			break;
		case KEY_SN:
		default:
			vfprintfq(fp, fmt, ap);
			break;
	}
}

static void
obj_start(FILE *fp, const char *name)
{
	assert(fp);

	if (name) {
		fprintfq(fp, "%s", name);
		fprintf(fp, ":");
	}
	fprintf(fp, "{");
}

static void
obj_end(FILE *fp)
{
	assert(fp);

	fprintf(fp, "}");
}

static void
array_start(FILE *fp, const char *name)
{
	assert(fp);

	if (name) {
		fprintfq(fp, "%s", name);
		fprintf(fp, ":");
	}

	fprintf(fp, "[");
}

static void
array_end(FILE *fp)
{
	assert(fp);

	fprintf(fp, "]");
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
json_serialize_v(FILE *fp, struct log_data_kv *kv, struct log_data_v *v)
{
	char s_addr[INET6_ADDRSTRLEN] = {'\0'};
	bool has_addr = false;
	int i;

	assert(fp);

	if (v->rx_src.sin6_family == AF_INET6) {
		inet_ntop(AF_INET6,
		    &v->rx_src.sin6_addr, s_addr, sizeof(s_addr));
		has_addr = true;
	}

	obj_start(fp, NULL);

        fprintfkv(fp, KEY_TS, "%ld.%09ld", v->ts.tv_sec, v->ts.tv_nsec);
	/*
	 * type: rx ... received from multicast
	 * type: tx ... transmit to decoder
	 */
	if (v->size == 0) {
		add_sep(fp);
		fprintfkv(fp, KEY_TYPE, "Receive");
	}
	else {
		add_sep(fp);
		fprintfkv(fp, KEY_TYPE, "Decode");
	}
	if (s_addr[0] != '\0') {
		add_sep(fp);
		fprintfkv(fp, KEY_SN, "%s", s_addr);
	}
	if (v->freq > 0) {
		add_sep(fp);
		fprintfkv(fp, KEY_FQ, "%u", v->freq);
	}
	if (v->dbm >= INT8_MIN && v->dbm <= INT8_MAX) {
		add_sep(fp);
		fprintfkv(fp, KEY_DB, "%d", v->dbm);
	}
	if (v->size > 0) {
		add_sep(fp);
		fprintfkv(fp, KEY_DS, "%u", v->size);
	}

	obj_end(fp);

	return 0;
}

int
json_serialize(FILE *fp, struct log_store *ls)
{
	struct log_data_kv *kv;
	struct log_data_v *v;
	bool first_kv = true;
	int indent = 0;

	if (fp == NULL)
		fp = stdout;

	array_start(fp, NULL);
	add_nl(fp);

	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		bool first_v = true;

		if (!first_kv) {
			add_sep(fp);
			add_nl(fp);
		}
		first_kv = false;

		v = TAILQ_FIRST(&kv->vh);

		obj_start(fp, NULL);

		fprintfkv(fp, KEY_SQ, "%llu", kv->key);
		add_sep(fp);
		add_nl(fp);

		fprintfkv(fp, KEY_BI, "%llu", v->block_idx);
		add_sep(fp);
		add_nl(fp);

		fprintfkv(fp, KEY_FI, "%llu", v->fragment_idx);
		add_sep(fp);
		add_nl(fp);

		fprintfkv(fp, KEY_IS_FEC, "%s",
		    kv->has_ethernet_frame ? "false" : "true");
		add_sep(fp);
		add_nl(fp);

		array_start(fp, "Event"); 
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (!first_v) {
				add_sep(fp);
				add_nl(fp);
			}
			first_v = false;
			json_serialize_v(fp, kv, v);
		}
		array_end(fp);
		add_nl(fp);
		obj_end(fp);
	}
	add_nl(fp);

	array_end(fp);
	add_nl(fp);

	return 0;
}
