#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sodium.h>

#include "frame_wfb.h"
#include "crypto_wfb.h"
#include "util_log.h"

struct crypto_wfb_context ctx = {
	.initialized = false
}; // singleton at this time;
   
static uint8_t _plain[MAX_DATA_PACKET_SIZE];
static unsigned long long _plain_len;

int
crypto_wfb_init(const char *keypair)
{
	FILE *fp = NULL;

	if (ctx.initialized)
		return 0;

	if (!keypair) {
		p_err("No keypair specified.\n");
		goto err;
	}
	fp = fopen(keypair, "r");
	if (fp == NULL) {
		p_err("Cannot open %s: %s\n", keypair, strerror(errno));
		goto err;
	}
	if (fread(ctx.secret_key, sizeof(ctx.secret_key), 1, fp) != 1) {
		p_err("Cannot read secret key: %s\n", strerror(errno));
		goto err;
	}
	if (fread(ctx.public_key, sizeof(ctx.public_key), 1, fp) != 1) {
		p_err("Cannot read public key: %s\n", strerror(errno));
		goto err;
	}
	fclose(fp);
	fp = NULL;

	if (sodium_init() < 0)  {
		p_err("Cannot initialize libsodium.\n");
		goto err;
	}

	ctx.has_secret_key = true;
	ctx.has_public_key = true;
	ctx.has_session_key = false;
	ctx.initialized = true;
	return 0;
err:
	if (fp)
		fclose(fp);
	return -1;
}

int
crypto_wfb_session_key_set(const uint8_t *key, size_t klen)
{
	ctx.has_session_key = false;
	if (klen != sizeof(ctx.session_key))
		return -1;

	memcpy(ctx.session_key, key, klen);
	ctx.has_session_key = true;
	return 0;
}

int
crypto_wfb_session_decrypt(uint8_t *dst, const uint8_t *src, uint64_t len,
    uint8_t *nonce)
{
	int r;

	if (dst == NULL || src == NULL || len == 0 || nonce == NULL)
		return -1;
	if (!ctx.initialized || !ctx.has_secret_key || !ctx.has_public_key)
		return -1;

	r = crypto_box_open_easy(dst, src, len, nonce,
	    ctx.public_key, ctx.secret_key);
	if (r != 0) {
		p_err("Failed to decrypt session. Check your keypair.\n");
		return -1;
	}

	return 0;
}

int
crypto_wfb_data_decrypt(uint8_t *dst, unsigned long long *dstlen,
    const uint8_t *src, uint64_t srclen, uint64_t hdrlen, uint8_t *nonce)
{
	int r;
	const uint8_t *srcp;
	uint8_t *dstp;
	unsigned long long *dstlenp;

	if (dst == NULL || src == NULL ||
	    *dstlen == 0 || srclen == 0 || hdrlen > srclen || nonce == NULL)
		return -1;
	if (!ctx.initialized || !ctx.has_session_key)
		return -1;

	srcp = src + hdrlen;
	srclen -= hdrlen;

	if (srcp == dst) {
		_plain_len = sizeof(_plain);
		dstp = _plain;
		dstlenp = &_plain_len;
	}
	else {
		dstp = dst;
		dstlenp = dstlen;
	}
	r = crypto_aead_chacha20poly1305_decrypt(dstp, dstlenp, NULL,
			srcp, srclen, src, hdrlen, nonce, ctx.session_key);
	if (r != 0) {
		p_err("Falied to decrypt data. Stale session key?\n");
		return -1;
	}
	if (srcp != dst)
		return 0;

	if (_plain_len > *dstlen) {
		p_err("Buffer exhausted.\n");
		return -1;
	}
	memcpy(dst, _plain, _plain_len);
	*dstlen = _plain_len;

	return 0;
}
