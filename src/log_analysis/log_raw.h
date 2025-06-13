#ifndef __LOG_RAW_H__
#define __LOG_RAW_H__
#include <stdbool.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/queue.h>

struct log_data_kv {
	uint64_t key;
	bool has_ethernet_frame;
	bool has_corrupted_frame;
	TAILQ_HEAD(log_data_v_hd, log_data_v) vh;

	TAILQ_ENTRY(log_data_kv) chain;
};

struct log_data_v {
	struct timespec ts;
	uint8_t type;
	uint32_t size;
	uint64_t block_idx;
	uint64_t fragment_idx;
	struct sockaddr_in6 rx_src;
	uint16_t freq;
	int16_t dbm;
	bool corrupt;
	void *buf;

	struct log_data_kv *kv;
	TAILQ_ENTRY(log_data_v) chain;
};

struct log_store {
	/* session info */
	uint32_t channel_id;
	uint8_t fec_type;
	uint8_t fec_k;
	uint8_t fec_n;
	struct timespec epoch;

	/* summary of raw packets */
	uint32_t n_pkts;
	uint32_t n_pkts_with_dbm;
	int16_t max_dbm;
	int16_t min_dbm;

	/* summary of H.265 frames */
	uint32_t n_frames;
	uint32_t max_frame_size;
	uint32_t min_frame_size;
	uint64_t total_bytes;

	TAILQ_HEAD(log_data_kv_hd, log_data_kv) kvh;
};

struct log_store *load_log(FILE *fp);
void free_log(struct log_store *ls);

#endif /* __LOG_RAW_H__ */
