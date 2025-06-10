#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>

#include "wfb_params.h"
#include "rx_core.h"
#include "compat.h"
#include "util_msg.h"

#include "log_raw.h"

static void
dump_header(struct rx_log_header *hd)
{
	assert(hd);

	p_debug("--- New Header---\n");
	p_debug("SEQ: %llu\n", hd->seq);
	p_debug("TimeStamp: %ld.%09ld\n", hd->ts.tv_sec, hd->ts.tv_nsec);
	p_debug("Size: %u\n", hd->size);
	p_debug("Block: %llu\n", hd->block_idx);
	p_debug("Fragemnt: %u\n", hd->fragment_idx);
	p_debug("FEC_K: %u\n", hd->fec_k);
	p_debug("FEC_N: %u\n", hd->fec_n);
	p_debug("FREQ: %u\n", hd->freq);
	p_debug("dbm: %d\n", hd->dbm);
}

static struct log_data_v *
log_v_alloc(struct log_store *ds, uint64_t key)
{
	struct log_data_kv *kv = NULL, *kvp = NULL;
	struct log_data_v *v;

	TAILQ_FOREACH_REVERSE(kv, &ds->kvh, log_data_kv_hd, chain) {
		if (kv->key > key) {
			kvp = kv;
			continue;
		}

		if (kv->key == key) {
			/* use existing kv */
			break;
		}
		/* no key found. create new kv and insert it before the kvp */
		kv = NULL;
		break;
	}
	if (!kv || kv->key != key) {
		kv = (struct log_data_kv *)malloc(sizeof(*kv));
		if (kv == NULL)
			return NULL;
		memset(kv, 0, sizeof(*kv));
		kv->key = key;
		kv->has_ethernet_frame = false;
		TAILQ_INIT(&kv->vh);

		if (kvp) {
			TAILQ_INSERT_BEFORE(kvp, kv, chain);
		}
		else {
			TAILQ_INSERT_TAIL(&ds->kvh, kv, chain);
		}
	}

	v = (struct log_data_v *)malloc(sizeof(*v));
	if (v == NULL)
		return NULL;
	memset(v, 0, sizeof(*v));
	v->kv = kv;
	TAILQ_INSERT_TAIL(&kv->vh, v, chain);
	return v;
}

static int
process_payload(FILE *fp, struct log_data_v *v, ssize_t size)
{
	assert(fp);
	assert(size >= 0 && size < WIFI_MTU);

	if (size == 0)
		return 0;
	if (v->buf != NULL) {
		p_info("Duplicated payload.\n");
		return 0;
	}

	v->buf = malloc(size);
	if (v->buf == NULL)
		return -1;

	if (fread(v->buf, size, 1, fp) <= 0) {
		free(v->buf);
		v->buf = NULL;
		v->size = 0;
		if (feof(fp)) {
			p_debug("End of File\n");
			return -1;
		}
		else if (ferror(fp)) {
			p_err("%s\n", strerror(errno));
			return -1;
		}
		p_err("Unknown I/O failure.\n");
		return -1;
	}

	return 0;
}

static ssize_t
process_header(FILE *fp, struct log_store *ls)
{
	struct rx_log_header hd;
	struct log_data_v *v;
	static struct timespec epoch = {
		.tv_sec = 0,
		.tv_nsec = 0
	};

	assert(fp);

	if (fread(&hd, sizeof(hd), 1, fp) <= 0) {
		if (feof(fp)) {
			p_debug("End of File\n");
			return -1;
		}
		else if (ferror(fp)) {
			p_err("%s\n", strerror(errno));
			return -1;
		}
		p_err("Unknown I/O failure.\n");
		return -1;
	}
	if (epoch.tv_sec == 0) {
		epoch = hd.ts;
	}
	timespecsub(&hd.ts, &epoch, &hd.ts);
	dump_header(&hd);

	v = log_v_alloc(ls, hd.seq);
	if (v == NULL)
		return -1;

	assert(v->kv);

	if (hd.size > 0) {
		v->size = hd.size;
	}
	else {
		v->kv->has_ethernet_frame = true;
	}
	if (v->ts.tv_sec == 0 && v->ts.tv_nsec == 0)
		v->ts = hd.ts;
	v->block_idx = hd.block_idx;
	v->fragment_idx = hd.fragment_idx;
	if (hd.freq != 0)
		v->freq = hd.freq;
	if (hd.dbm != INT16_MIN)
		v->dbm = hd.dbm;
	else
		v->dbm = INT16_MIN;

	if (hd.rx_src.sin6_family == AF_INET6) {
		v->rx_src = hd.rx_src;
	}

	if (process_payload(fp, v, hd.size) < 0)
		return -1;

	// update counters
	if (hd.size > 0) {
		/* H.265 frames */
		ls->n_frames++;
		if (ls->max_frame_size < hd.size)
			ls->max_frame_size = hd.size;
		if (ls->min_frame_size > hd.size)
			ls->min_frame_size = hd.size;
		ls->total_bytes += hd.size;
	}
	else {
		/* Raw ethernet frames */
		ls->n_pkts++;
		if (hd.dbm != INT16_MIN) {
			ls->n_pkts_with_dbm++;
			if (ls->max_dbm < hd.dbm)
				ls->max_dbm = hd.dbm;
			if (ls->min_dbm > hd.dbm)
				ls->min_dbm = hd.dbm;
		}
	}

	return hd.size;
}

struct log_store *
load_log(FILE *fp)
{
	struct log_store *ls;
	ssize_t size;

	if (fp == NULL)
		fp = stdin;

	ls = (struct log_store *)malloc(sizeof(*ls));
	if (ls == NULL)
		return NULL;

	memset(ls, 0, sizeof(*ls));
	ls->max_dbm = INT16_MIN;
	ls->min_dbm = INT16_MAX;
	ls->max_frame_size = 0;
	ls->min_frame_size = UINT32_MAX;
	TAILQ_INIT(&ls->kvh);

	for (;;) {
		size = process_header(fp, ls);
		if (size < 0)
			break;
	}

	return ls;
}

void
free_log(struct log_store *ls)
{
	struct log_data_kv *kv, *kvp;
	struct log_data_v *v, *vp;

	if (!ls)
		return;

	TAILQ_FOREACH_SAFE(kv, &ls->kvh, chain, kvp) {
		TAILQ_FOREACH_SAFE(v, &kv->vh, chain, vp) {
			if (v->buf)
				free(v->buf);
			TAILQ_REMOVE(&kv->vh, v, chain);
			free(v);
		}
		TAILQ_REMOVE(&ls->kvh, kv, chain);
		free(kv);
	}
	free(ls);
}
