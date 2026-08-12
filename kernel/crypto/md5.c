// ============================================================
// md5.c - MD5 (RFC 1321) + KAT (see md5.h). Legacy INTEROP checksum, NOT for security.
// ============================================================
#include "md5.h"

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

// Per-round left-rotate amounts (RFC 1321 section 3.4).
static const uint8_t S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

// K[i] = floor(2^32 * abs(sin(i + 1))), i = 0..63 (RFC 1321 section 3.4).
static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static void md5_transform(md5_ctx_t* ctx, const uint8_t data[]) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++)                 // MD5 reads the block little-endian
        m[i] = (uint32_t)data[i*4] | ((uint32_t)data[i*4+1] << 8)
             | ((uint32_t)data[i*4+2] << 16) | ((uint32_t)data[i*4+3] << 24);

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if      (i < 16) { f = (b & c) | (~b & d);        g = i;                }
        else if (i < 32) { f = (d & b) | (~d & c);        g = (5*i + 1) & 15;   }
        else if (i < 48) { f = b ^ c ^ d;                 g = (3*i + 5) & 15;   }
        else             { f = c ^ (b | ~d);              g = (7*i)     & 15;   }
        f = f + a + K[i] + m[g];
        a = d; d = c; c = b;
        b = b + ROTL32(f, S[i]);
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
}

void md5_init(md5_ctx_t* ctx) {
    ctx->datalen = 0;
    ctx->bitlen  = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void md5_update(md5_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == MD5_BLOCK_SIZE) {
            md5_transform(ctx, ctx->data);
            ctx->bitlen += MD5_BLOCK_SIZE * 8;
            ctx->datalen = 0;
        }
    }
}

void md5_final(md5_ctx_t* ctx, uint8_t hash[MD5_DIGEST_SIZE]) {
    uint64_t bits = ctx->bitlen + (uint64_t)ctx->datalen * 8;
    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56) {                      // no room for the length -> flush a block
        while (ctx->datalen < MD5_BLOCK_SIZE) ctx->data[ctx->datalen++] = 0;
        md5_transform(ctx, ctx->data);
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56) ctx->data[ctx->datalen++] = 0;
    for (int i = 0; i < 8; i++)                   // 64-bit little-endian bit length
        ctx->data[56 + i] = (uint8_t)(bits >> (8 * i));
    md5_transform(ctx, ctx->data);

    for (int i = 0; i < 4; i++) {                 // output each word little-endian
        hash[i]      = (uint8_t)(ctx->state[0] >> (8 * i));
        hash[4 + i]  = (uint8_t)(ctx->state[1] >> (8 * i));
        hash[8 + i]  = (uint8_t)(ctx->state[2] >> (8 * i));
        hash[12 + i] = (uint8_t)(ctx->state[3] >> (8 * i));
    }
}

void md5(const uint8_t* msg, uint32_t len, uint8_t out[MD5_DIGEST_SIZE]) {
    md5_ctx_t c;
    md5_init(&c);
    md5_update(&c, msg, len);
    md5_final(&c, out);
}

// ---- known-answer self-test (`md5`) ----
int md5_selftest(void) {
    // The seven vectors of the RFC 1321 appendix A.5 "MD5 test suite".
    struct { const char* msg; uint8_t md[16]; } sv[] = {
        { "", { 0xd4,0x1d,0x8c,0xd9,0x8f,0x00,0xb2,0x04,
                0xe9,0x80,0x09,0x98,0xec,0xf8,0x42,0x7e } },
        { "a", { 0x0c,0xc1,0x75,0xb9,0xc0,0xf1,0xb6,0xa8,
                 0x31,0xc3,0x99,0xe2,0x69,0x77,0x26,0x61 } },
        { "abc", { 0x90,0x01,0x50,0x98,0x3c,0xd2,0x4f,0xb0,
                   0xd6,0x96,0x3f,0x7d,0x28,0xe1,0x7f,0x72 } },
        { "message digest", { 0xf9,0x6b,0x69,0x7d,0x7c,0xb7,0x93,0x8d,
                              0x52,0x5a,0x2f,0x31,0xaa,0xf1,0x61,0xd0 } },
        { "abcdefghijklmnopqrstuvwxyz",
              { 0xc3,0xfc,0xd3,0xd7,0x61,0x92,0xe4,0x00,
                0x7d,0xfb,0x49,0x6c,0xca,0x67,0xe1,0x3b } },
        { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
              { 0xd1,0x74,0xab,0x98,0xd2,0x77,0xd9,0xf5,
                0xa5,0x61,0x1c,0x2c,0x9f,0x41,0x9d,0x9f } },
        { "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
              { 0x57,0xed,0xf4,0xa2,0x2b,0xe3,0xc9,0x55,
                0xac,0x49,0xda,0x2e,0x21,0x07,0xb6,0x7a } },
    };
    for (int i = 0; i < 7; i++) {
        uint32_t n = 0; while (sv[i].msg[n]) n++;
        uint8_t out[16];
        md5((const uint8_t*)sv[i].msg, n, out);
        for (int j = 0; j < 16; j++) if (out[j] != sv[i].md[j]) return i + 1;
    }

    // Incremental update must agree with the one-shot: "abc" fed as "a" then "bc".
    md5_ctx_t c; uint8_t out[16];
    md5_init(&c);
    md5_update(&c, (const uint8_t*)"a", 1);
    md5_update(&c, (const uint8_t*)"bc", 2);
    md5_final(&c, out);
    static const uint8_t abc[16] = { 0x90,0x01,0x50,0x98,0x3c,0xd2,0x4f,0xb0,
                                     0xd6,0x96,0x3f,0x7d,0x28,0xe1,0x7f,0x72 };
    for (int j = 0; j < 16; j++) if (out[j] != abc[j]) return 8;

    return 0;
}
