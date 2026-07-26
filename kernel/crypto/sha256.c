#include "sha256.h"

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(a,b)   (((a) >> (b)) | ((a) << (32 - (b))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)     (ROTR(x,2)  ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x)     (ROTR(x,6)  ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x)    (ROTR(x,7)  ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x)    (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

static void sha256_transform(sha256_ctx_t* ctx, const uint8_t block[SHA256_BLOCK_SIZE]) {
    uint32_t W[64];
    for (int i = 0; i < 16; i++)
        W[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8)  |  (uint32_t)block[i*4+3];
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    uint32_t a = ctx->state[0], b = ctx->state[1];
    uint32_t c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5];
    uint32_t g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t T1 = h + EP1(e) + CH(e,f,g) + K[i] + W[i];
        uint32_t T2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + T1;
        d = c; c = b; b = a; a = T1 + T2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t* ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += SHA256_BLOCK_SIZE * 8;
            ctx->datalen = 0;
        }
    }
}

void sha256_final(sha256_ctx_t* ctx, uint8_t hash[SHA256_DIGEST_SIZE]) {
    uint64_t bits = ctx->bitlen + ctx->datalen * 8;
    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56) {
        while (ctx->datalen < SHA256_BLOCK_SIZE)
            ctx->data[ctx->datalen++] = 0;
        sha256_transform(ctx, ctx->data);
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56)
        ctx->data[ctx->datalen++] = 0;
    ctx->data[56] = bits >> 56;
    ctx->data[57] = bits >> 48;
    ctx->data[58] = bits >> 40;
    ctx->data[59] = bits >> 32;
    ctx->data[60] = bits >> 24;
    ctx->data[61] = bits >> 16;
    ctx->data[62] = bits >> 8;
    ctx->data[63] = bits;
    sha256_transform(ctx, ctx->data);

    for (int i = 0; i < 4; i++) {
        hash[i]    = ctx->state[0] >> (24 - i*8);
        hash[4+i]  = ctx->state[1] >> (24 - i*8);
        hash[8+i]  = ctx->state[2] >> (24 - i*8);
        hash[12+i] = ctx->state[3] >> (24 - i*8);
        hash[16+i] = ctx->state[4] >> (24 - i*8);
        hash[20+i] = ctx->state[5] >> (24 - i*8);
        hash[24+i] = ctx->state[6] >> (24 - i*8);
        hash[28+i] = ctx->state[7] >> (24 - i*8);
    }
}

void hmac_sha256(const uint8_t* key, uint32_t keylen,
                 const uint8_t* msg, uint32_t msglen,
                 uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t k[SHA256_BLOCK_SIZE];
    if (keylen > SHA256_BLOCK_SIZE) {
        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, keylen);
        sha256_final(&ctx, k);
        for (int i = SHA256_DIGEST_SIZE; i < SHA256_BLOCK_SIZE; i++)
            k[i] = 0;
    } else {
        for (uint32_t i = 0; i < keylen; i++) k[i] = key[i];
        for (uint32_t i = keylen; i < SHA256_BLOCK_SIZE; i++) k[i] = 0;
    }

    uint8_t ipad[SHA256_BLOCK_SIZE];
    uint8_t opad[SHA256_BLOCK_SIZE];
    for (int i = 0; i < SHA256_BLOCK_SIZE; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, msg, msglen);
    uint8_t inner[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}

void pbkdf2_hmac_sha256(const uint8_t* password, uint32_t pwdlen,
                        const uint8_t* salt, uint32_t saltlen,
                        uint32_t iterations,
                        uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t U[SHA256_DIGEST_SIZE];
    uint8_t T[SHA256_DIGEST_SIZE];
    uint8_t block[SHA256_DIGEST_SIZE + 4];

    for (uint32_t i = 0; i < saltlen; i++) block[i] = salt[i];
    block[saltlen]     = 0;
    block[saltlen + 1] = 0;
    block[saltlen + 2] = 0;
    block[saltlen + 3] = 1;

    hmac_sha256(password, pwdlen, block, saltlen + 4, U);
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) T[i] = U[i];

    for (uint32_t i = 2; i <= iterations; i++) {
        hmac_sha256(password, pwdlen, U, SHA256_DIGEST_SIZE, U);
        for (int j = 0; j < SHA256_DIGEST_SIZE; j++)
            T[j] ^= U[j];
    }

    for (int i = 0; i < SHA256_DIGEST_SIZE; i++)
        out[i] = T[i];
}

void sha256_to_hex(const uint8_t hash[SHA256_DIGEST_SIZE], char* hex) {
    static const char hexdig[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        hex[i*2]     = hexdig[(hash[i] >> 4) & 0xF];
        hex[i*2 + 1] = hexdig[hash[i] & 0xF];
    }
    hex[SHA256_DIGEST_SIZE * 2] = '\0';
}
