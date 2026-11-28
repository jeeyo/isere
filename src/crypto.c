#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "crypto.h"

#include <string.h>

#include "mem.h"

#include "mbedtls/platform.h"
#include "mbedtls/md.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

static mbedtls_entropy_context __entropy;
static mbedtls_ctr_drbg_context __drbg;
static int __initialized = 0;

static void *__calloc(size_t n, size_t size)
{
  return isere_calloc(n, size);
}

int isere_crypto_init(void)
{
  if (__initialized) {
    return 0;
  }
  mbedtls_platform_set_calloc_free(__calloc, isere_free);
  mbedtls_entropy_init(&__entropy);
  mbedtls_ctr_drbg_init(&__drbg);
  static const char personalization[] = "isere";
  if (mbedtls_ctr_drbg_seed(&__drbg, mbedtls_entropy_func, &__entropy,
      (const unsigned char *)personalization, sizeof(personalization) - 1) != 0) {
    return -1;
  }
  __initialized = 1;
  return 0;
}

void isere_crypto_deinit(void)
{
  if (!__initialized) {
    return;
  }
  mbedtls_ctr_drbg_free(&__drbg);
  mbedtls_entropy_free(&__entropy);
  __initialized = 0;
}

void *isere_crypto_drbg(void)
{
  return __initialized ? &__drbg : NULL;
}

int isere_crypto_random(uint8_t *buf, size_t len)
{
  if (isere_crypto_init() < 0) {
    return -1;
  }
  while (len > 0) {
    size_t n = len > MBEDTLS_CTR_DRBG_MAX_REQUEST ? MBEDTLS_CTR_DRBG_MAX_REQUEST : len;
    if (mbedtls_ctr_drbg_random(&__drbg, buf, n) != 0) {
      return -1;
    }
    buf += n;
    len -= n;
  }
  return 0;
}

static int __b64_value(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  return -1;
}

int isere_base64url_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap)
{
  while (in_len > 0 && in[in_len - 1] == '=') {
    in_len--;
  }
  if (in_len % 4 == 1) {
    return -1;
  }

  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < in_len; i++) {
    int v = __b64_value(in[i]);
    if (v < 0) {
      return -1;
    }
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= out_cap) {
        return -1;
      }
      out[n++] = (uint8_t)(acc >> bits);
    }
  }
  return (int)n;
}

int isere_atob_utf8(const char *in, size_t in_len, char *out, size_t out_cap)
{
  // forgiving-base64 decode: ASCII whitespace is ignored, padding is optional
  size_t n = 0, digits = 0;
  uint32_t acc = 0;
  int bits = 0, padding = 0;
  for (size_t i = 0; i < in_len; i++) {
    char c = in[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') continue;
    if (c == '=') { padding++; continue; }
    if (padding > 0 || c == '-' || c == '_') return -1;
    int v = __b64_value(c);
    if (v < 0) return -1;
    digits++;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      uint8_t byte = (uint8_t)(acc >> bits);
      if (byte < 0x80) {
        if (n + 1 > out_cap) return -1;
        out[n++] = (char)byte;
      } else {
        if (n + 2 > out_cap) return -1;
        out[n++] = (char)(0xC0 | (byte >> 6));
        out[n++] = (char)(0x80 | (byte & 0x3F));
      }
    }
  }
  if (digits % 4 == 1 || padding > 2 || (padding > 0 && (digits + (size_t)padding) % 4 != 0)) {
    return -1;
  }
  return (int)n;
}

int isere_btoa_utf8(const char *in, size_t in_len, char *out, size_t out_cap)
{
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t n = 0;
  uint32_t acc = 0;
  int count = 0;
  for (size_t i = 0; i < in_len; i++) {
    uint8_t c = (uint8_t)in[i];
    uint32_t cp;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < in_len) {
      cp = ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)in[++i] & 0x3F);
    } else {
      return -1;
    }
    if (cp > 0xFF) return -1;
    acc = (acc << 8) | cp;
    if (++count == 3) {
      if (n + 4 > out_cap) return -1;
      out[n++] = b64[(acc >> 18) & 63];
      out[n++] = b64[(acc >> 12) & 63];
      out[n++] = b64[(acc >> 6) & 63];
      out[n++] = b64[acc & 63];
      acc = 0;
      count = 0;
    }
  }
  if (count > 0) {
    if (n + 4 > out_cap) return -1;
    acc <<= 8 * (3 - count);
    out[n++] = b64[(acc >> 18) & 63];
    out[n++] = b64[(acc >> 12) & 63];
    out[n++] = count == 2 ? b64[(acc >> 6) & 63] : '=';
    out[n++] = '=';
  }
  return (int)n;
}

static mbedtls_md_type_t __md_type(int hash)
{
  switch (hash) {
    case 256: return MBEDTLS_MD_SHA256;
    case 384: return MBEDTLS_MD_SHA384;
    case 512: return MBEDTLS_MD_SHA512;
    default: return MBEDTLS_MD_NONE;
  }
}

int isere_crypto_digest(int hash, const uint8_t *data, size_t len, uint8_t *out)
{
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(__md_type(hash));
  if (info == NULL || mbedtls_md(info, data, len, out) != 0) {
    return -1;
  }
  return (int)mbedtls_md_get_size(info);
}

// decodes a base64url key parameter into a fresh buffer
static uint8_t *__decode_param(const char *s, size_t len, size_t *out_len)
{
  size_t cap = len * 3 / 4 + 3;
  uint8_t *buf = isere_malloc(cap);
  if (buf == NULL) {
    return NULL;
  }
  int n = isere_base64url_decode(s, len, buf, cap);
  if (n <= 0) {
    isere_free(buf);
    return NULL;
  }
  *out_len = (size_t)n;
  return buf;
}

static int __verify_rsa(const uint8_t *n, size_t n_len, const uint8_t *e, size_t e_len,
  mbedtls_md_type_t md, const uint8_t *hash, size_t hash_len,
  const uint8_t *sig, size_t sig_len, const char **error)
{
  mbedtls_rsa_context rsa;
  mbedtls_rsa_init(&rsa);
  int ret = -1;

  if (mbedtls_rsa_import_raw(&rsa, n, n_len, NULL, 0, NULL, 0, NULL, 0, e, e_len) != 0 ||
      mbedtls_rsa_complete(&rsa) != 0 || mbedtls_rsa_check_pubkey(&rsa) != 0) {
    *error = "invalid RSA public key";
    goto done;
  }
  if (mbedtls_rsa_get_len(&rsa) < 256) {
    // RS256 keys under 2048 bits are not accepted
    *error = "RSA key is shorter than 2048 bits";
    goto done;
  }
  if (sig_len != mbedtls_rsa_get_len(&rsa)) {
    ret = 0;
    goto done;
  }
  ret = mbedtls_rsa_pkcs1_verify(&rsa, md, (unsigned int)hash_len, hash, sig) == 0 ? 1 : 0;

done:
  mbedtls_rsa_free(&rsa);
  return ret;
}

static int __verify_p256(const uint8_t *x, size_t x_len, const uint8_t *y, size_t y_len,
  const uint8_t *hash, size_t hash_len, const uint8_t *sig, size_t sig_len, const char **error)
{
  mbedtls_ecp_group grp;
  mbedtls_ecp_point q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  int ret = -1;

  if (x_len != 32 || y_len != 32) {
    *error = "invalid P-256 public key";
    goto done;
  }
  if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
      mbedtls_mpi_read_binary(&q.MBEDTLS_PRIVATE(X), x, x_len) != 0 ||
      mbedtls_mpi_read_binary(&q.MBEDTLS_PRIVATE(Y), y, y_len) != 0 ||
      mbedtls_mpi_lset(&q.MBEDTLS_PRIVATE(Z), 1) != 0 ||
      mbedtls_ecp_check_pubkey(&grp, &q) != 0) {
    *error = "invalid P-256 public key";
    goto done;
  }
  if (sig_len != 64) {
    ret = 0;
    goto done;
  }
  if (mbedtls_mpi_read_binary(&r, sig, 32) != 0 || mbedtls_mpi_read_binary(&s, sig + 32, 32) != 0) {
    ret = 0;
    goto done;
  }
  ret = mbedtls_ecdsa_verify(&grp, hash, hash_len, &q, &r, &s) == 0 ? 1 : 0;

done:
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&q);
  mbedtls_ecp_group_free(&grp);
  return ret;
}

int isere_crypto_verify(isere_key_kind_t kind, int hash,
  const char *a, size_t a_len, const char *b, size_t b_len,
  const uint8_t *sig, size_t sig_len, const uint8_t *data, size_t data_len,
  const char **error)
{
  *error = NULL;
  mbedtls_md_type_t md = __md_type(hash);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(md);
  if (info == NULL) {
    *error = "unsupported hash";
    return -1;
  }

  uint8_t digest[64];
  if (mbedtls_md(info, data, data_len, digest) != 0) {
    *error = "hash failed";
    return -1;
  }
  size_t digest_len = mbedtls_md_get_size(info);

  size_t pa_len = 0, pb_len = 0;
  uint8_t *pa = __decode_param(a, a_len, &pa_len);
  uint8_t *pb = __decode_param(b, b_len, &pb_len);
  int ret;
  if (pa == NULL || pb == NULL) {
    *error = "invalid key encoding";
    ret = -1;
  } else if (kind == ISERE_KEY_RSA) {
    ret = __verify_rsa(pa, pa_len, pb, pb_len, md, digest, digest_len, sig, sig_len, error);
  } else {
    ret = __verify_p256(pa, pa_len, pb, pb_len, digest, digest_len, sig, sig_len, error);
  }
  isere_free(pa);
  isere_free(pb);
  return ret;
}
