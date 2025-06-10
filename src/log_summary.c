#include <stdio.h>
#include <stdlib.h>

#include "util_msg.h"

#include "log_summary.h"

int
summary_output(FILE *fp, struct log_store *ls)
{
	struct log_data_kv *kv;
	int n_fec = 0;

	if (fp == NULL)
		fp = stdout;

	fprintf(fp, "Number of Ethernet Frames: %u\n", ls->n_pkts);
	fprintf(fp, "Number of Ethernet Frames(with dbm): %u\n",
	    ls->n_pkts_with_dbm);
	fprintf(fp, "Maximum dbm: %d\n", ls->max_dbm);
	fprintf(fp, "minimum dbm: %d\n", ls->min_dbm);
	fprintf(fp, "Number of H.265 Frames: %u\n", ls->n_frames);
	fprintf(fp, "Maximum frame size: %u\n", ls->max_frame_size);
	fprintf(fp, "minimum frame size: %u\n", ls->min_frame_size);
	fprintf(fp, "Total H.265 bytes: %llu\n", ls->total_bytes);

	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		if (kv->has_ethernet_frame)
			continue;
		p_debug("Sequence %llu recovered by FEC\n", kv->key);
		n_fec++;
	}
	fprintf(fp, "Frame recovered using FEC: %d\n", n_fec);

	return 0;
}
