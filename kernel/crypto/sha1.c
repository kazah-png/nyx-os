// ============================================================
// sha1.c - SHA-1 (FIPS 180) + HMAC-SHA-1 (RFC 2104) + KAT (see sha1.h)
// ============================================================
#include "sha1.h"

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(sha1_ctx_t* ctx, const uint8_t data[]) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)data[i*4] << 24) | ((uint32_t)data[i*4+1] << 16)
             | ((uint32_t)data[i*4+2] << 8) | (uint32_t)data[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = ROTL32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2],
             d = ctx->state[3], e = ctx->state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f = (b & c) | ((~b) & d);          k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                      k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);    k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                      k = 0xCA62C1D6; }
        uint32_t tmp = ROTL32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROTL32(b, 30); b = a; a = tmp;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e;
}

void sha1_init(sha1_ctx_t* ctx) {
    ctx->datalen = 0;
    ctx->bitlen  = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

void sha1_update(sha1_ctx_t* ctx, const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == SHA1_BLOCK_SIZE) {
            sha1_transform(ctx, ctx->data);
            ctx->bitlen += SHA1_BLOCK_SIZE * 8;
            ctx->datalen = 0;
        }
    }
}

void sha1_final(sha1_ctx_t* ctx, uint8_t hash[SHA1_DIGEST_SIZE]) {
    uint64_t bits = ctx->bitlen + (uint64_t)ctx->datalen * 8;
    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56) {
        while (ctx->datalen < SHA1_BLOCK_SIZE) ctx->data[ctx->datalen++] = 0;
        sha1_transform(ctx, ctx->data);
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56) ctx->data[ctx->datalen++] = 0;
    ctx->data[56] = (uint8_t)(bits >> 56);
    ctx->data[57] = (uint8_t)(bits >> 48);
    ctx->data[58] = (uint8_t)(bits >> 40);
    ctx->data[59] = (uint8_t)(bits >> 32);
    ctx->data[60] = (uint8_t)(bits >> 24);
    ctx->data[61] = (uint8_t)(bits >> 16);
    ctx->data[62] = (uint8_t)(bits >> 8);
    ctx->data[63] = (uint8_t)bits;
    sha1_transform(ctx, ctx->data);

    for (uint32_t i = 0; i < 4; i++) {
        hash[i]    = (uint8_t)(ctx->state[0] >> (24 - i*8));
        hash[4+i]  = (uint8_t)(ctx->state[1] >> (24 - i*8));
        hash[8+i]  = (uint8_t)(ctx->state[2] >> (24 - i*8));
        hash[12+i] = (uint8_t)(ctx->state[3] >> (24 - i*8));
        hash[16+i] = (uint8_t)(ctx->state[4] >> (24 - i*8));
    }
}

// HMAC-SHA-1 (RFC 2104): block size 64. Long keys are pre-hashed, short keys zero-padded.
void hmac_sha1(const uint8_t* key, uint32_t keylen,
               const uint8_t* msg, uint32_t msglen,
               uint8_t out[SHA1_DIGEST_SIZE]) {
    uint8_t k[SHA1_BLOCK_SIZE];
    if (keylen > SHA1_BLOCK_SIZE) {
        sha1_ctx_t kc;
        sha1_init(&kc);
        sha1_update(&kc, key, keylen);
        sha1_final(&kc, k);
        for (int i = SHA1_DIGEST_SIZE; i < SHA1_BLOCK_SIZE; i++) k[i] = 0;
    } else {
        for (uint32_t i = 0; i < keylen; i++) k[i] = key[i];
        for (uint32_t i = keylen; i < SHA1_BLOCK_SIZE; i++) k[i] = 0;
    }

    uint8_t ipad[SHA1_BLOCK_SIZE], opad[SHA1_BLOCK_SIZE];
    for (int i = 0; i < SHA1_BLOCK_SIZE; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    sha1_ctx_t ctx;
    uint8_t inner[SHA1_DIGEST_SIZE];
    sha1_init(&ctx);
    sha1_update(&ctx, ipad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, msg, msglen);
    sha1_final(&ctx, inner);

    sha1_init(&ctx);
    sha1_update(&ctx, opad, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, inner, SHA1_DIGEST_SIZE);
    sha1_final(&ctx, out);
}

// ---- known-answer self-test (`sha1`) ----
static int eq20(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 20; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int sha1_selftest(void) {
    // SHA-1, FIPS 180 appendix examples.
    struct { const char* msg; uint8_t md[20]; } sv[] = {
        { "",    { 0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,
                   0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09 } },
        { "abc", { 0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
                   0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d } },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                 { 0x84,0x98,0x3e,0x44,0x1c,0x3b,0xd2,0x6e,0xba,0xae,
                   0x4a,0xa1,0xf9,0x51,0x29,0xe5,0xe5,0x46,0x70,0xf1 } },
    };
    for (int i = 0; i < 3; i++) {
        uint32_t n = 0; while (sv[i].msg[n]) n++;
        sha1_ctx_t c; uint8_t out[20];
        sha1_init(&c); sha1_update(&c, (const uint8_t*)sv[i].msg, n); sha1_final(&c, out);
        if (!eq20(out, sv[i].md)) return 1;
    }

    // HMAC-SHA-1, RFC 2202 test cases 1-3.
    uint8_t out[20];
    uint8_t k1[20]; for (int i = 0; i < 20; i++) k1[i] = 0x0b;
    const uint8_t exp1[20] = { 0xb6,0x17,0x31,0x86,0x55,0x05,0x72,0x64,0xe2,0x8b,
                               0xc0,0xb6,0xfb,0x37,0x8c,0x8e,0xf1,0x46,0xbe,0x00 };
    hmac_sha1(k1, 20, (const uint8_t*)"Hi There", 8, out);
    if (!eq20(out, exp1)) return 2;

    const uint8_t exp2[20] = { 0xef,0xfc,0xdf,0x6a,0xe5,0xeb,0x2f,0xa2,0xd2,0x74,
                               0x16,0xd5,0xf1,0x84,0xdf,0x9c,0x25,0x9a,0x7c,0x79 };
    hmac_sha1((const uint8_t*)"Jefe", 4,
              (const uint8_t*)"what do ya want for nothing?", 28, out);
    if (!eq20(out, exp2)) return 3;

    uint8_t k3[20]; for (int i = 0; i < 20; i++) k3[i] = 0xaa;
    uint8_t d3[50]; for (int i = 0; i < 50; i++) d3[i] = 0xdd;
    const uint8_t exp3[20] = { 0x12,0x5d,0x73,0x42,0xb9,0xac,0x11,0xcd,0x91,0xa3,
                               0x9a,0xf4,0x8a,0xa1,0x7b,0x4f,0x63,0xf1,0x75,0xd3 };
    hmac_sha1(k3, 20, d3, 50, out);
    if (!eq20(out, exp3)) return 4;

    return 0;
}
