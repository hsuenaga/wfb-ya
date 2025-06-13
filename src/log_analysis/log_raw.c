#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>

#include "../wfb_params.h"
#include "../rx_log.h"
#include "../compat.h"
#include "../util_msg.h"

#include "log_raw.h"

static void
dump_header(struct rx_log_frame_header *hd)
{
	assert(hd);

	p_debug("--- New Header---\n");
	p_debug("SEQ: %" PRIu64 "\n", le64toh(hd->seq));
	p_debug("TimeStamp: %" PRIu64 ".%09" PRIu64 "\n",
	    le64toh(hd->tv_sec), le64toh(hd->tv_nsec));
	p_debug("Size: %u\n", le32toh(hd->size));
	p_debug("Block: %" PRIu64 "\n", le64toh(hd->block_idx));
	p_debug("Fragemnt: %u\n", hd->fragment_idx);
	p_debug("FREQ: %u\n", le16toh(hd->freq));
	p_debug("dbm: %d\n", le16toh(hd->dbm));
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
process_file_header(FILE *fp, struct log_store *ls)
{
	struct rx_log_file_header hd;

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
	if (le32toh(hd.signature) != RX_LOG_SIGNATURE) {
		p_err("Invalid file signature.\n");
		return -1;
	}
	if (hd.version != RX_LOG_VERSION) {
		p_err("Unknown Log version.\n");
		return -1;
	}
	ls->channel_id = le32toh(hd.channel_id);
	ls->fec_type = hd.fec_type;
	ls->fec_k = hd.fec_k;
	ls->fec_n = hd.fec_n;

	return sizeof(hd);
}

static ssize_t
process_frame_header(FILE *fp, struct log_store *ls)
{
	struct rx_log_frame_header hd;
	struct log_data_v *v;
	struct timespec ts;

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
	dump_header(&hd);
	ts.tv_sec = (time_t)(le64toh(hd.tv_sec));
	ts.tv_nsec = (long)(le64toh(hd.tv_nsec));
	if (ls->epoch.tv_sec == 0) {
		ls->epoch = ts;
	}
	timespecsub(&ts, &ls->epoch, &ts);

	v = log_v_alloc(ls, hd.seq);
	if (v == NULL)
		return -1;

	assert(v->kv);

	v->size = hd.size;

	if (v->ts.tv_sec == 0 && v->ts.tv_nsec == 0)
		v->ts = ts;
	v->block_idx = hd.block_idx;
	v->fragment_idx = hd.fragment_idx;
	v->freq = hd.freq;
	v->dbm = hd.dbm;
	v->type = hd.type;

	switch (hd.type) {
	case FRAME_TYPE_CORRUPT:
		v->kv->has_ethernet_frame = true;
		v->kv->has_corrupted_frame = true;
		v->rx_src.sin6_family = AF_INET6;
		v->rx_src.sin6_port = 0;
		v->corrupt = true;
		memcpy(&v->rx_src.sin6_addr, hd.rx_src,
		    sizeof(v->rx_src.sin6_addr));
		break;
	case FRAME_TYPE_INET6:
		v->kv->has_ethernet_frame = true;
		v->rx_src.sin6_family = AF_INET6;
		v->rx_src.sin6_port = 0;
		memcpy(&v->rx_src.sin6_addr, hd.rx_src,
		    sizeof(v->rx_src.sin6_addr));

		ls->n_pkts++;
		if (hd.dbm != DBM_INVAL) {
			ls->n_pkts_with_dbm++;
			if (ls->max_dbm < hd.dbm)
				ls->max_dbm = hd.dbm;
			if (ls->min_dbm > hd.dbm)
				ls->min_dbm = hd.dbm;
		}
		break;
	case FRAME_TYPE_DECODE:
		if (process_payload(fp, v, hd.size) < 0)
			return -1;

		ls->n_frames++;
		if (ls->max_frame_size < hd.size)
			ls->max_frame_size = hd.size;
		if (ls->min_frame_size > hd.size)
			ls->min_frame_size = hd.size;
		ls->total_bytes += hd.size;
		break;
	default:
		p_err("Unknown frame type %d.\n", hd.type);
		break;
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
	ls->max_dbm = DBM_MIN;
	ls->min_dbm = DBM_MAX;
	ls->max_frame_size = FRAME_SIZE_MIN;
	ls->min_frame_size = FRAME_SIZE_MAX;
	TAILQ_INIT(&ls->kvh);

	if (process_file_header(fp, ls) < 0) {
		free(ls);
		return NULL;
	}

	for (;;) {
		size = process_frame_header(fp, ls);
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
