#ifndef ISERE_CRYPTO_H_
#define ISERE_CRYPTO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

// Native side of the crypto.subtle subset in src/prelude.js, backed by mbedTLS.
// Engine-neutral: the bindings only pass bytes in and out.

int isere_crypto_init(void);
void isere_crypto_deinit(void);

typedef enum { ISERE_KEY_RSA = 0, ISERE_KEY_EC_P256 } isere_key_kind_t;

// hash: 256, 384 or 512 (SHA-2)
// RSA: a = modulus n, b = exponent e; EC P-256: a = x, b = y. Both base64url, as in a JWK.
// For ECDSA the signature is r || s (the WebCrypto and JWS format).
// Returns 1 if the signature is valid, 0 if not, and <0 if the key is unusable (*error says why).
int isere_crypto_verify(isere_key_kind_t kind, int hash,
  const char *a, size_t a_len, const char *b, size_t b_len,
  const uint8_t *sig, size_t sig_len, const uint8_t *data, size_t data_len,
  const char **error);

// SHA-2 digest into out (64 bytes room); returns the digest length or <0
int isere_crypto_digest(int hash, const uint8_t *data, size_t len, uint8_t *out);

int isere_crypto_random(uint8_t *buf, size_t len);

// the DRBG, for TLS (an mbedtls_ctr_drbg_context *)
void *isere_crypto_drbg(void);

// base64url (or base64) without padding requirements; returns the decoded length or <0
int isere_base64url_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* ISERE_CRYPTO_H_ */
