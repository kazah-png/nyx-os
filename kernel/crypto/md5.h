#ifndef MD5_H
#define MD5_H

#include "../core/kernel.h"

// MD5 (RFC 1321). MD5 is cryptographically BROKEN for collision resistance, so it must
// NOT be used for signatures, certificates or any security decision. It is here purely for
// INTEROP: a great deal of the existing world still emits MD5 as a non-adversarial content
// checksum — package/file manifests, HTTP `Content-MD5`, HTTP Digest auth (RFC 2617),
// legacy config and download-verification tooling. NyxOS had SHA-1/256/512/3 and BLAKE2s
// but could not read or produce an MD5 digest at all; this fills that gap. The digest is
// little-endian (unlike the big-endian SHA family). Pinned by md5_selftest().

#define MD5_DIGEST_SIZE 16
#define MD5_BLOCK_SIZE  64

typedef struct {
    uint8_t  data[MD5_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[4];
} md5_ctx_t;

void md5_init(md5_ctx_t* ctx);
void md5_update(md5_ctx_t* ctx, const uint8_t* data, uint32_t len);
void md5_final(md5_ctx_t* ctx, uint8_t hash[MD5_DIGEST_SIZE]);

// One-shot convenience: hash msg[0..len) into out[16].
void md5(const uint8_t* msg, uint32_t len, uint8_t out[MD5_DIGEST_SIZE]);

int md5_selftest(void);   // KAT: the full RFC 1321 "MD5 test suite"

#endif
