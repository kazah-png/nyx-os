#ifndef SHA1_H
#define SHA1_H

#include "../core/kernel.h"

// SHA-1 (FIPS 180) + HMAC-SHA-1 (RFC 2104). SHA-1 is cryptographically retired for
// collision resistance, so this is NOT for new signatures — it is here for INTEROP:
// HMAC-SHA-1 is the default MAC of HOTP/TOTP (RFC 4226/6238), which is what the common
// authenticator apps (Google Authenticator, etc.) actually use, and it still shows up in
// legacy TLS and git. NyxOS already had HMAC-SHA-256/512 but no SHA-1, so it could not
// speak the most widespread TOTP variant. Pinned by sha1_selftest().

#define SHA1_DIGEST_SIZE 20
#define SHA1_BLOCK_SIZE  64

typedef struct {
    uint8_t  data[SHA1_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[5];
} sha1_ctx_t;

void sha1_init(sha1_ctx_t* ctx);
void sha1_update(sha1_ctx_t* ctx, const uint8_t* data, uint32_t len);
void sha1_final(sha1_ctx_t* ctx, uint8_t hash[SHA1_DIGEST_SIZE]);

void hmac_sha1(const uint8_t* key, uint32_t keylen,
               const uint8_t* msg, uint32_t msglen,
               uint8_t out[SHA1_DIGEST_SIZE]);

int sha1_selftest(void);   // KAT: FIPS 180 SHA-1 vectors + RFC 2202 HMAC-SHA-1 vectors

#endif
