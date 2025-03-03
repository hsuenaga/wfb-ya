#ifndef __UTIL_RINGBUF_H__
#define __UTIL_RINGBUF_H__
#include <stdint.h>

struct rbuf;

struct rbuf_block {
	uint64_t index;
	size_t fragment_used;
	size_t fragment_to_send;
	uint8_t **fragment;
	size_t *fragment_len;

	struct rbuf *rbuf;
};

struct rbuf {
	size_t ring_size;
	size_t ring_front;
	size_t ring_alloc;

	size_t fragment_nof;
	size_t fragment_size;

	uint64_t last_block;

	struct rbuf_block *blocks;
};

#define BLOCK_INVAL ((uint64_t)-1)

extern struct rbuf *rbuf_alloc(size_t ring_size,
    size_t frag_size, size_t nfrag);
extern void rbuf_free(struct rbuf *rbuf);

extern struct rbuf_block *rbuf_get_block(struct rbuf *rbuf,
    uint64_t block_idx);
extern void rbuf_free_block(struct rbuf_block *block);

static inline struct rbuf_block *rbuf_get_front(struct rbuf *rbuf) {
	return &rbuf->blocks[rbuf->ring_front];
}

static inline int rbuf_block_is_front(struct rbuf_block *block) {
	struct rbuf *rbuf = block->rbuf;

	return (block == rbuf_get_front(rbuf));
}

#endif /* __UTIL_RINGBUF_H__ */
