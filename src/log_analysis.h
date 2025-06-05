#ifndef __LOG_ANALYSIS_H__
#define __LOG_ANALYSIS_H__
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/queue.h>

struct log_analysis_opt {
	char *file_name;
};


struct log_data {
	uint64_t key;

	struct timespec ts;
	uint32_t size;
	uint64_t block_idx;
	uint64_t fragment_idx;
	struct sockaddr_in6 rx_src;
	void *buf;

	STAILQ_ENTRY(log_data) next;
};

struct log_store {
	STAILQ_HEAD(log_data_hd, log_data) dh;
};
#endif /* __LOG_ANALYSIS_H__ */
