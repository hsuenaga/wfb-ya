#ifndef __CRYPTO_WFB__
#define __CRYPTO_WFB__
#include <stdbool.h>
#include <sodium.h>

struct crypto_wfb_context {
	bool initialized;
	bool has_secret_key;
	bool has_public_key;
	bool has_session_key;
	uint8_t secret_key[crypto_box_SECRETKEYBYTES];
	uint8_t public_key[crypto_box_PUBLICKEYBYTES];
	uint8_t session_key[crypto_aead_chacha20poly1305_KEYBYTES];
};

extern int crypto_wfb_init(const char *keypair);
extern int crypto_wfb_session_key_set(const uint8_t *key, size_t klen);
extern int crypto_wfb_session_decrypt(uint8_t *dst, const uint8_t *src,
    uint64_t len, uint8_t *nonce);
extern int crypto_wfb_data_decrypt(uint8_t *dst, unsigned long long *dstlen,
    const uint8_t *src, uint64_t srclen, uint64_t hdrlen, uint8_t *nonce);
#endif /* __CRYPTO_WFB__ */
