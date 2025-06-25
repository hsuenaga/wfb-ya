#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <yyjson.h>

#include "../util_attribute.h"
#include "../compat.h"

#include "../rx_log.h"
#include "log_analysis.h"
#include "log_raw.h"
#include "log_csv.h"
#include "log_json.h"

static const char *
s_v_type(struct log_data_v *v)
{
	assert(v);

	switch (v->type) {
	case FRAME_TYPE_CORRUPT:
		return "Corrupt";
	case FRAME_TYPE_INET6:
		return "Receive";
	case FRAME_TYPE_DECODE:
		return "Decode";
	default:
		break;
	}

	return "Unknown";
}

static const char *
s_source(struct log_data_v *v)
{
	static char s_addr[INET6_ADDRSTRLEN];

	assert(v);

	if (v->rx_src.sin6_family == AF_INET6) {
		s_addr[0] = '\0';
		inet_ntop(AF_INET6,
		    &v->rx_src.sin6_addr, s_addr, sizeof(s_addr));
	}
	else {
		return NULL;
	}

	return s_addr;
}

static float
f_timestamp(struct log_data_v *v)
{
	uint64_t nsec;
	float fsec;

	nsec = v->ts.tv_sec * 1000 * 1000 * 1000;
	nsec += v->ts.tv_nsec;

	fsec = ((float)nsec / (1000.0 * 1000.0 * 1000.0));

	return fsec;
}

static yyjson_mut_val *
wfb_mut_obj_kv_seq(struct yyjson_mut_doc *doc, struct log_data_kv *kv)
{
	struct log_data_v *v;
	yyjson_mut_val *kvj;
	yyjson_mut_val *vj;
	yyjson_mut_val *evj;
	const char *s;

	assert(doc);
	assert(kv);

	v = TAILQ_FIRST(&kv->vh);
	if (!v)
		return NULL;

	kvj = yyjson_mut_obj(doc);
	evj = yyjson_mut_arr(doc);

	yyjson_mut_obj_add_uint(doc, kvj, "Key", kv->key);
	yyjson_mut_obj_add_uint(doc, kvj, "BlockIndex", v->block_idx);
	yyjson_mut_obj_add_uint(doc, kvj, "FragmentIndex", v->fragment_idx);
	yyjson_mut_obj_add_bool(doc, kvj, "IsParity", v->is_parity);
	yyjson_mut_obj_add_bool(doc, kvj, "IsFEC", kv->has_fec_frame);
	yyjson_mut_obj_add_uint(doc, kvj, "EthernetFrames",
	    kv->n_ethernet_frame);
	yyjson_mut_obj_add_uint(doc, kvj, "H265Frames", kv->n_h265_frame);
	TAILQ_FOREACH(v, &kv->vh, chain) {
		vj = yyjson_mut_obj(doc);

		yyjson_mut_obj_add_float(doc, vj, "TimeStamp", f_timestamp(v));
		yyjson_mut_obj_add_str(doc, vj, "Type", s_v_type(v));
		s = s_source(v);
		if (s) {
			yyjson_mut_obj_add_strcpy(doc, vj, "SourceNode", s);
		}
		if (v->freq > 0) {
			yyjson_mut_obj_add_uint(doc, vj, "SourceNode", v->freq);
		}
		if (v->dbm >= DBM_MIN && v->dbm <= DBM_MAX) {
			yyjson_mut_obj_add_int(doc, vj, "dbm", v->dbm);
		}
		if (v->size > 0) {
			yyjson_mut_obj_add_uint(doc, vj, "DataSize", v->size);
		}

		yyjson_mut_arr_append(evj, vj);
	}
	yyjson_mut_obj_add_val(doc, kvj, "Event", evj);

	return kvj;
}

static yyjson_mut_val *
wfb_mut_obj_kv_blk(struct yyjson_mut_doc *doc, struct log_data_kv *kv)
{
	struct log_data_v *v;
	yyjson_mut_val *kvj;
	yyjson_mut_val *vj;
	yyjson_mut_val *evj;
	const char *s;

	assert(doc);
	assert(kv);

	v = TAILQ_FIRST(&kv->vh);
	if (!v)
		return NULL;

	kvj = yyjson_mut_obj(doc);
	evj = yyjson_mut_arr(doc);

	yyjson_mut_obj_add_uint(doc, kvj, "Key", kv->key);
	yyjson_mut_obj_add_bool(doc, kvj, "HasLostFrame", kv->has_lost_frame);
	yyjson_mut_obj_add_bool(doc, kvj, "IsFEC", kv->has_fec_frame);
	yyjson_mut_obj_add_uint(doc, kvj, "EthernetFrames",
	    kv->n_ethernet_frame);
	yyjson_mut_obj_add_uint(doc, kvj, "H265Frames", kv->n_h265_frame);
	TAILQ_FOREACH(v, &kv->vh, block_chain) {
		vj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_float(doc, vj, "TimeStamp", f_timestamp(v));
		yyjson_mut_obj_add_uint(doc, vj, "BlockIndex", v->block_idx);
		yyjson_mut_obj_add_uint(doc, vj, "FragmentIndex",
		    v->fragment_idx);
		yyjson_mut_obj_add_str(doc, vj, "Type", s_v_type(v));
		s = s_source(v);
		if (s) {
			yyjson_mut_obj_add_str(doc, vj, "SourceNode", s);
		}
		if (v->freq > 0) {
			yyjson_mut_obj_add_uint(doc, vj, "SourceNode", v->freq);
		}
		if (v->dbm >= DBM_MIN && v->dbm <= DBM_MAX) {
			yyjson_mut_obj_add_int(doc, vj, "dbm", v->dbm);
		}
		if (v->size > 0) {
			yyjson_mut_obj_add_uint(doc, vj, "DataSize", v->size);
		}

		yyjson_mut_arr_append(evj, vj);
	}
	yyjson_mut_obj_add_val(doc, kvj, "Event", evj);

	return kvj;
}

__unused static yyjson_mut_doc *
kvh2yyjson(struct log_data_kv_hd *kvh)
{
	struct log_data_kv *kv;
	struct yyjson_mut_doc *doc;
	struct yyjson_mut_val *root;
	struct yyjson_mut_val *kvj;

	doc = yyjson_mut_doc_new(NULL);
	root = yyjson_mut_arr(doc);

	TAILQ_FOREACH(kv, kvh, chain) {
		switch (kv->type) {
			case KV_TYPE_SEQ:
				kvj = wfb_mut_obj_kv_seq(doc, kv);
				break;
			case KV_TYPE_BLK:
				kvj = wfb_mut_obj_kv_blk(doc, kv);
				break;
			default:
				kvj = NULL;
				break;
		}
		if (!kvj)
			continue;
		yyjson_mut_arr_append(root, kvj);
	}
	yyjson_mut_doc_set_root(doc, root);

	return doc;
}

static int
json_serialize_kvh(FILE *fp, struct log_data_kv_hd *kvh)
{
	struct yyjson_mut_doc *doc;
	yyjson_write_flag flg;
	yyjson_write_err err;

	if (fp == NULL)
		fp = stdout;

	doc = kvh2yyjson(kvh);
	if (!doc)
		return -1;

	flg = YYJSON_WRITE_PRETTY | YYJSON_WRITE_ESCAPE_UNICODE;
	yyjson_mut_write_fp(fp, doc, flg, NULL, &err);
	if (err.code) {
		    p_info("JSON write error (%u): %s\n", err.code, err.msg);
		    return -1;
	}
	yyjson_mut_doc_free(doc);
	fprintf(fp, "\n");
	fflush(fp);

	return 0;
}

int
json_serialize(FILE *fp, struct log_store *ls)
{
	return json_serialize_kvh(fp, &ls->kvh);
}

int
json_serialize_block(FILE *fp, struct log_store *ls)
{
	return json_serialize_kvh(fp, &ls->block_kvh);
}
