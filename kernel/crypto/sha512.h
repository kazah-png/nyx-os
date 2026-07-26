#ifndef SHA512_H
#define SHA512_H
// SHA-512 and SHA-384 (FIPS 180-4). See sha512.c. Needed for the ECDSA-P384/SHA-384 links
// in real TLS certificate chains.
#include "../core/kernel.h"

typedef struct {
    uint64_t state[8];
    uint8_t  buf[128];
    uint32_t buflen;
    uint64_t total;      // total bytes (message length fits in 64 bits for our uses)
} sha512_ctx_t;

void sha512_init(sha512_ctx_t* c);
void sha384_init(sha512_ctx_t* c);
void sha512_update(sha512_ctx_t* c, const uint8_t* data, uint32_t len);
void sha512_final(sha512_ctx_t* c, uint8_t out[64]);
void sha384_final(sha512_ctx_t* c, uint8_t out[48]);

void sha512(const uint8_t* data, uint32_t len, uint8_t out[64]);
void sha384(const uint8_t* data, uint32_t len, uint8_t out[48]);

int  sha512_selftest(void);

#endif
