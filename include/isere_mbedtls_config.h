#ifndef ISERE_MBEDTLS_CONFIG_H_
#define ISERE_MBEDTLS_CONFIG_H_

// mbedTLS for isère: JWT signature verification (crypto.subtle) and a TLS 1.2
// client for fetch(). Everything else is left out to keep the image small.

// allocations go through isère's accounting allocator (mbedtls_platform_set_calloc_free)
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_ERROR_C

// hashes
#define MBEDTLS_MD_C
#define MBEDTLS_SHA1_C        /* some CA certificates are still signed with it */
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

// RS256 and ES256
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

// certificates
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

// randomness
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_ENTROPY_C
#if !defined(__linux__)
// the board has no /dev/urandom; entropy comes from mbedtls_hardware_poll()
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#endif

// ciphers for TLS 1.2
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C

// TLS 1.2 client with ECDHE key exchange
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC

// servers may send full 16 KB records; requests are small
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

#if defined(__linux__)
// certificate validity periods are checked against the clock
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
// the CA bundle is read from a file (ISERE_CA_FILE)
#define MBEDTLS_FS_IO
#endif

#endif /* ISERE_MBEDTLS_CONFIG_H_ */
