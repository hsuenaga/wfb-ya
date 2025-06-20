#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include "../rx_log.h"
#include "log_raw.h"
#include "log_hist.h"

const char *
graph(uint32_t hist, uint32_t hist_Max)
{
	int n;

	n = hist * 10 / hist_Max;

	switch (n) {
		case 0:
			return "|";
		case 1:
			return "|*";
		case 2:
			return "|**";
		case 3:
			return "|***";
		case 4:
			return "|****";
		case 5:
			return "|*****";
		case 6:
			return "|******";
		case 7:
			return "|*******";
		case 8:
			return "|********";
		case 9:
			return "|*********";
		case 10:
		default:
			return "|**********";
	};
	
}

int
log_hist(struct log_store *ls)
{
	struct log_data_kv *kv;
	struct log_data_v *v;
	uint32_t hist[UINT8_MAX], cumulative[UINT8_MAX], mode, hist_Max;
	int nentry = 0, sum = 0, half;
	uint8_t idx, idx_min = UINT8_MAX, idx_Max = 0, idx_mode;
	uint8_t idx_median = 0;
	int8_t dbm;
	float mean;

	memset(hist, 0, sizeof(hist));
	TAILQ_FOREACH(kv, &ls->kvh, chain) {
		TAILQ_FOREACH(v, &kv->vh, chain) {
			if (v->type != FRAME_TYPE_INET6)
				continue;
			if (v->dbm == DBM_INVAL)
				continue;
			if (v->filtered)
				continue;
			idx = v->dbm - INT8_MIN;
			hist[idx]++;
			if (idx_min > idx)
				idx_min = idx;
			if (idx_Max < idx)
				idx_Max = idx;
			nentry++;
			sum += v->dbm;
		}
	}
	half = nentry / 2;

	hist_Max = 0;
	cumulative[0] = hist[0];
	for (idx = 1; idx < UINT8_MAX; idx++) {
		cumulative[idx] = cumulative[idx - 1] + hist[idx];
		if (idx_median == 0 && cumulative[idx] > half) {
			idx_median = idx;
		}
		if (hist_Max < hist[idx])
			hist_Max = hist[idx];
	}

	p_info("%3s\t%10s\t%10s\t%-11s\n",
	    "dbm", "Count", "Cumulative", "Graph");
	mode = 0;
	idx_mode = idx_min;
	for (idx = idx_min; idx <= idx_Max; idx++) {
		dbm = idx + INT8_MIN;
		p_info("%03d\t%10u\t%10u\t%-11s\n",
		   dbm, hist[idx], cumulative[idx], graph(hist[idx], hist_Max));
		if (mode < hist[idx]) {
			idx_mode = idx;
			mode = hist[idx];
		}
	}

	mean = (float)sum / (float)nentry;

	p_info("\n");
	p_info("Mean %6.3f, Median %d, Mode %d\n",
	    mean, idx_median + INT8_MIN, idx_mode + INT8_MIN);

	return 0;
}

