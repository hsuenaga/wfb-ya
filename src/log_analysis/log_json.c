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

#include "../rx_log.h"
#include "log_analysis.h"
#include "log_raw.h"
#include "log_csv.h"
#include "log_json.h"

const char *json_keys[] = {
	"Key",
	"TimeStamp",
	"BlockIndex",
	"FragmentIndex",
	"SourceNode",
	"Frequency",
	"dbm",
	"DataSize",
	"IsFEC",
	"Type",
	"EthernetFrames",
	"H265Frames",
	"IsParity",
	"HasLostFrame",
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
			/* treat as integer */
			vfprintf(fp, fmt, ap);
			break;
		case KEY_IS_FEC:
		case KEY_IS_PARITY:
		case KEY_IS_LOST:
			/* treat as boolean */
			vfprintf(fp, fmt, ap);
			break;
		default:
			/* treat as string */
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
	const char *s_type;
	bool has_addr = false;
	int i;

	assert(fp);

	if (v->rx_src.sin6_family == AF_INET6) {
		inet_ntop(AF_INET6,
		    &v->rx_src.sin6_addr, s_addr, sizeof(s_addr));
		has_addr = true;
	}

	obj_start(fp, NULL);

	/* timestamp */
        fprintfkv(fp, KEY_TS, "%ld.%09ld", v->ts.tv_sec, v->ts.tv_nsec);

	/* block index and fragment index */
	if (kv->type == KV_TYPE_BLK) {
		add_sep(fp);
		fprintfkv(fp, KEY_BI, "%" PRIu64 "", v->block_idx);
		add_sep(fp);
		fprintfkv(fp, KEY_FI, "%" PRIu64 "", v->fragment_idx);
		if (v->type == FRAME_TYPE_INET6) {
			add_sep(fp);
			fprintfkv(fp, KEY_IS_PARITY, "%s",
			    v->is_parity ? "true" : "false");
		}
	}

	/* event type */
	switch (v->type) {
	case FRAME_TYPE_CORRUPT:
		s_type = "Corrupt";
		break;
	case FRAME_TYPE_INET6:
		s_type = "Receive";
		break;
	case FRAME_TYPE_DECODE:
		s_type = "Decode";
		break;
	default:
		s_type = "Unknown";
		break;
	}
	add_sep(fp);
	fprintfkv(fp, KEY_TYPE, "%s", s_type);

	/* address */
	if (s_addr[0] != '\0') {
		add_sep(fp);
		fprintfkv(fp, KEY_SN, "%s", s_addr);
	}

	/* frequency */
	if (v->freq > 0) {
		add_sep(fp);
		fprintfkv(fp, KEY_FQ, "%u", v->freq);
	}

	/* dbm */
	if (v->dbm >= DBM_MIN && v->dbm <= DBM_MAX) {
		add_sep(fp);
		fprintfkv(fp, KEY_DB, "%d", v->dbm);
	}

	/* size */
	if (v->size > 0) {
		add_sep(fp);
		fprintfkv(fp, KEY_DS, "%u", v->size);
	}

	obj_end(fp);

	return 0;
}

static int
json_serialize_event(FILE *fp, struct log_data_kv *kv)
{
	struct log_data_v *v;
	bool first_v = true;

	array_start(fp, "Event"); 

	if (kv->type == KV_TYPE_BLK) {
		TAILQ_FOREACH(v, &kv->vh, block_chain) {
			if (!first_v) {
				add_sep(fp);
				add_nl(fp);
			}
			first_v = false;
			json_serialize_v(fp, kv, v);
		}
	}
	else {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (!first_v) {
				add_sep(fp);
				add_nl(fp);
			}
			first_v = false;
			json_serialize_v(fp, kv, v);
		}
	}

	array_end(fp);

	return 0;
}

static int
json_serialize_kvh(FILE *fp, struct log_store *ls, struct log_data_kv_hd *kvh)
{
	struct log_data_kv *kv;
	struct log_data_v *v;
	bool first_kv = true;
	int indent = 0;

	if (fp == NULL)
		fp = stdout;

	array_start(fp, NULL);
	add_nl(fp);

	TAILQ_FOREACH(kv, kvh, chain) {
		if (!first_kv) {
			add_sep(fp);
			add_nl(fp);
		}
		first_kv = false;

		v = TAILQ_FIRST(&kv->vh);

		obj_start(fp, NULL);

		fprintfkv(fp, KEY_SQ, "%" PRIu64 "", kv->key);
		add_sep(fp);
		add_nl(fp);

		if (kv->type == KV_TYPE_SEQ) {
			fprintfkv(fp, KEY_BI, "%" PRIu64 "", v->block_idx);
			add_sep(fp);
			add_nl(fp);

			fprintfkv(fp, KEY_FI, "%" PRIu64 "", v->fragment_idx);
			add_sep(fp);
			add_nl(fp);

			fprintfkv(fp, KEY_IS_PARITY, "%s",
			    v->is_parity ? "true" : "false");
			add_sep(fp);
			add_nl(fp);
		}
		else {
			fprintfkv(fp, KEY_IS_LOST, "%s",
			    kv->has_lost_frame ? "true" : "false");
			add_sep(fp);
			add_nl(fp);
		}

		fprintfkv(fp, KEY_IS_FEC,
		    kv->has_fec_frame ? "true" : "false");
		add_sep(fp);
		add_nl(fp);


		fprintfkv(fp, KEY_N_ETHER, "%d", kv->n_ethernet_frame);
		add_sep(fp);
		add_nl(fp);

		fprintfkv(fp, KEY_N_H265, "%d", kv->n_h265_frame);
		add_sep(fp);
		add_nl(fp);

		json_serialize_event(fp, kv);
		add_nl(fp);

		obj_end(fp);
	}
	add_nl(fp);

	array_end(fp);
	add_nl(fp);

	return 0;
}

int
json_serialize(FILE *fp, struct log_store *ls)
{
	return json_serialize_kvh(fp, ls, &ls->kvh);
}

int
json_serialize_block(FILE *fp, struct log_store *ls)
{
	return json_serialize_kvh(fp, ls, &ls->block_kvh);
}
