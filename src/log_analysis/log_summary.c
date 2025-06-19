#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "../frame_wfb.h"
#include "../util_msg.h"

#include "log_summary.h"

static const char *
s_fectype(uint8_t type)
{
	switch (type) {
	case WFB_FEC_VDM_RS:
		return "VDM_RS (Reed-Solomon over Vandermonde Matrix)";
	default:
		break;
	}

	return "Unknown";
}

int
summary_output(FILE *fp, struct log_store *ls)
{
	struct log_data_kv *kv;
	int n_fec = 0;
	int n_lost = 0;

	if (fp == NULL)
		fp = stdout;

	fprintf(fp, "---ORIGINAL DATA---\n");
	fprintf(fp, "Channel ID: %u\n", ls->channel_id);
	fprintf(fp, "FEC_TYPE: %s\n", s_fectype(ls->fec_type));
	fprintf(fp, "FEC_K: %u\n", ls->fec_k);
	fprintf(fp, "FEC_N: %u\n", ls->fec_n);
	fprintf(fp, "Number of Ethernet Frames: %u\n", ls->n_pkts);
	fprintf(fp, "Number of Ethernet Frames(with dbm): %u\n",
	    ls->n_pkts_with_dbm);
	fprintf(fp, "Maximum dbm: %d\n", ls->max_dbm);
	fprintf(fp, "minimum dbm: %d\n", ls->min_dbm);
	fprintf(fp, "Number of H.265 Frames: %u\n", ls->n_frames);
	fprintf(fp, "Maximum frame size: %u\n", ls->max_frame_size);
	fprintf(fp, "minimum frame size: %u\n", ls->min_frame_size);
	fprintf(fp, "Total H.265 bytes: %" PRIu64 "\n", ls->total_bytes);

	fprintf(fp, "---AFTER FILTER---\n");
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		if (!kv->has_fec_frame)
			continue;
		p_debug("Sequence %" PRIu64 " recovered by FEC\n", kv->key);
		n_fec++;
	}
	fprintf(fp, "Frame recovered using FEC: %d\n", n_fec);

	TAILQ_FOREACH(kv, &ls->block_kvh, chain) {
		if (!kv->has_lost_frame)
			continue;
		p_debug("Block %" PRIu64 " has lost frames\n", kv->key);
		n_lost++;
	}
	fprintf(fp, "Number of corrupted blocks: %d\n", n_lost);

	return 0;
}
