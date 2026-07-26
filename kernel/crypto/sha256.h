#ifndef SHA256_H
#define SHA256_H

#include "../core/kernel.h"

#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE  64
#define PBKDF2_ITERATIONS  10000   /* KDF work factor; stored per passwd entry, so raising it stays backward-compatible */

typedef struct {
    uint8_t  data[SHA256_BLOCK_SIZE];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t* ctx);
void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, uint32_t len);
void sha256_final(sha256_ctx_t* ctx, uint8_t hash[SHA256_DIGEST_SIZE]);

void hmac_sha256(const uint8_t* key, uint32_t keylen,
                 const uint8_t* msg, uint32_t msglen,
                 uint8_t out[SHA256_DIGEST_SIZE]);

void pbkdf2_hmac_sha256(const uint8_t* password, uint32_t pwdlen,
                        const uint8_t* salt, uint32_t saltlen,
                        uint32_t iterations,
                        uint8_t out[SHA256_DIGEST_SIZE]);

void sha256_to_hex(const uint8_t hash[SHA256_DIGEST_SIZE], char* hex);

#endif
