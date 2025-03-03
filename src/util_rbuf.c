#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "util_rbuf.h"
#include "util_log.h"

struct rbuf *
rbuf_alloc(size_t ring_size, size_t frag_size, size_t nfrag)
{
	struct rbuf *rbuf;
	int i;

	rbuf = (struct rbuf *)malloc(sizeof(*rbuf));
	if (rbuf == NULL)
		return NULL;
	rbuf->ring_size = ring_size;
	rbuf->ring_front = 0;
	rbuf->ring_alloc = 0;

	rbuf->fragment_nof = nfrag;
	rbuf->fragment_size = frag_size;
	rbuf->last_block = BLOCK_INVAL;

	rbuf->blocks =
	    (struct rbuf_block *)calloc(ring_size, sizeof(struct rbuf_block));
	if (rbuf->blocks == NULL) {
		goto err;
	}
	for (i = 0; i < ring_size; i++) {
		struct rbuf_block *blk = &rbuf->blocks[i];
		int j;

		blk->index = BLOCK_INVAL;
		blk->fragment =
		    (uint8_t **)calloc(nfrag, sizeof(uint8_t *));
		if (blk->fragment == NULL)
			goto err;
		for (j = 0; j < nfrag; j++) {
			blk->fragment[j] = (uint8_t *)malloc(frag_size);
			if (blk->fragment[j] == NULL)
				goto err;
		}
		blk->fragment_len =
		    (size_t *)calloc(nfrag, sizeof(size_t));
		if (blk->fragment_len == NULL)
			goto err;
		blk->rbuf = rbuf;
	}

	return rbuf;
err:
	rbuf_free(rbuf);
	return NULL;

}

void
rbuf_free(struct rbuf *rbuf)
{
	if (rbuf == NULL)
		return;

	if (rbuf->blocks) {
		int i;

		for (i = 0; i < rbuf->ring_size; i++) {
			struct rbuf_block *blk = &rbuf->blocks[i];

			if (blk->fragment) {
				int j;

				for (j = 0; j < rbuf->fragment_nof; j++) {
					if (blk->fragment[j])
						free(blk->fragment[j]);
				}
				free(blk->fragment);
			}
			if (blk->fragment_len)
				free(blk->fragment_len);
		}
		free(rbuf->blocks);
	}

	free(rbuf);
}

struct rbuf_block *
rbuf_get_block(struct rbuf *rbuf, uint64_t block_idx)
{
	int new_blocks;
	uint64_t allocate_start;
	size_t idx;
	int i;

	for (i = 0; i < rbuf->ring_alloc; i++) {
		idx = (rbuf->ring_front + i) % rbuf->ring_size;

		if (rbuf->blocks[idx].index == block_idx)
			return &rbuf->blocks[idx];
	}
	if (rbuf->last_block != BLOCK_INVAL && block_idx <= rbuf->last_block) {
		// out of the sliding window.
		return NULL;
	}

	// should be new block(s)
	new_blocks = (rbuf->last_block == BLOCK_INVAL) ?
	    1 : block_idx - rbuf->last_block;
	if (new_blocks > rbuf->ring_size)
		new_blocks = rbuf->ring_size;
	allocate_start = block_idx - new_blocks + 1;

	// force allocate blocks. existing blocks are dropped silently..
	for (i = 0; i < new_blocks; i++)
	{
		struct rbuf_block *blk;

		idx = (rbuf->ring_front + rbuf->ring_alloc + i) %
		    rbuf->ring_size;
		blk = &rbuf->blocks[idx];

		// drop
		if (blk->index != BLOCK_INVAL)
			rbuf_free_block(blk);

		// new
		blk->index = (allocate_start + i);
		blk->fragment_used = 0;
		blk->fragment_to_send = 0;
		memset(blk->fragment_len, 0,
		    sizeof(size_t) * rbuf->fragment_nof);
		rbuf->ring_alloc++;
	}

	// idx is point to block_idx here.
	rbuf->last_block = block_idx;
	return &rbuf->blocks[idx];
}

void
rbuf_free_block(struct rbuf_block *block)
{
	struct rbuf *rbuf;

	if (block == NULL)
		return;
       	rbuf = block->rbuf;
	if (rbuf == NULL) {
		p_err("broken data structure\n");
		return; 
	}

	block->index = BLOCK_INVAL;

	while (rbuf->blocks[rbuf->ring_front].index == BLOCK_INVAL) {
		rbuf->ring_alloc--;
		if (rbuf->ring_alloc == 0)
			break;
		rbuf->ring_front = (rbuf->ring_front + 1) % rbuf->ring_size;
	}
}
