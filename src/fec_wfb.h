#ifndef __FEC_WFB_H__
#define __FEC_WFB_H__
#include <fec.h>
#include "util_rbuf.h"

struct fec_context {
	int type;

	union {
		struct zfec_context {
			fec_t *zfec;
		} zfec;
	} u;
};

extern int fec_wfb_init(void);
extern int fec_wfb_new(struct fec_context *ctx, int type, int k, int n);
extern int fec_wfb_apply(struct fec_context *ctx,
    const uint8_t **in, uint8_t **out, unsigned *index, size_t size);
#endif /* __FEC_WFB_H__ */
