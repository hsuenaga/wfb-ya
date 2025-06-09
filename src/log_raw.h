#ifndef __LOG_RAW_H__
#define __LOG_RAW_H__
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/queue.h>

struct log_data_kv {
	uint64_t key;
	TAILQ_HEAD(log_data_v_hd, log_data_v) vh;

	TAILQ_ENTRY(log_data_kv) chain;
};

struct log_data_v {
	struct timespec ts;
	uint32_t size;
	uint64_t block_idx;
	uint64_t fragment_idx;
	struct sockaddr_in6 rx_src;
	uint16_t freq;
	int16_t dbm;
	void *buf;

	TAILQ_ENTRY(log_data_v) chain;
};

struct log_store {
	TAILQ_HEAD(log_data_kv_hd, log_data_kv) kvh;
};

struct log_store *load_log(FILE *fp);
void free_log(struct log_store *ls);

#endif /* __LOG_RAW_H__ */
