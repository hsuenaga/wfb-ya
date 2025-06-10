#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "frame_wfb.h"
#include "fec_wfb.h"
#include "util_rbuf.h"
#include "util_msg.h"

static int
fec_zfec_new(struct fec_context *ctx, int k, int n)
{
	struct zfec_context *zctx = &ctx->u.zfec;

	if (zctx->zfec) {
		fec_free(zctx->zfec);
		zctx->zfec = NULL;
	}
	zctx->zfec = fec_new(k, n);
	if (!zctx->zfec) {
		p_err("fec_new() failed.\n");
		return -1;
	}

	return 0;
}

int fec_zfec_decode(struct fec_context *ctx,
    const uint8_t **in, uint8_t **out, unsigned *index, size_t size)
{
	struct zfec_context *zctx;

	if (!ctx)
		return -1;

	zctx = &ctx->u.zfec;
	if (!zctx)
		return -1;

	fec_decode(zctx->zfec, in, out, index, size);
	return 0;
}

int
fec_wfb_init(void)
{
	fec_init();

	return 0;
}

int
fec_wfb_new(struct fec_context *ctx, int type, int k, int n)
{
	assert(ctx);

	switch (type) {
		case WFB_FEC_VDM_RS:
			ctx->type = type;
			return fec_zfec_new(ctx, k, n);
		default:
			break;
	}

	return -1;
}

int fec_wfb_apply(struct fec_context *ctx,
    const uint8_t **in, uint8_t **out, unsigned *index, size_t size)
{
	assert(ctx);
	assert(in);
	assert(out);
	assert(index);
	assert(size);

	switch (ctx->type) {
		case WFB_FEC_VDM_RS:
			return fec_zfec_decode(ctx, in, out, index, size);
		default:
			break;
	}

	return -1;
}
