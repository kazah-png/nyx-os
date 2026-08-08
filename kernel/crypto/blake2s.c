// ============================================================
// BLAKE2s-256 (RFC 7693), unkeyed, single-shot.
// ============================================================
// Eight 32-bit state words are seeded from the SHA-256 IV XORed with the
// parameter block (digest length 32, unkeyed, fanout/depth 1). The message is
// absorbed 64 bytes at a time by the ARX compression function F — ten rounds of
// eight ChaCha-style G mixes over the 16 message words permuted by SIGMA — with
// a byte counter and a final-block flag. No tables, no data-dependent branches.

#include "blake2s.h"

static const uint32_t IV[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
};

static const uint8_t SIGMA[10][16] = {
    {  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
    { 14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3 },
    { 11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4 },
    {  7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8 },
    {  9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13 },
    {  2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9 },
    { 12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11 },
    { 13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10 },
    {  6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0 },
};

static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint32_t load32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void store32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#define G(a, b, c, d, x, y) do {                                  \
    v[a] = v[a] + v[b] + (x); v[d] = rotr32(v[d] ^ v[a], 16);      \
    v[c] = v[c] + v[d];       v[b] = rotr32(v[b] ^ v[c], 12);      \
    v[a] = v[a] + v[b] + (y); v[d] = rotr32(v[d] ^ v[a], 8);       \
    v[c] = v[c] + v[d];       v[b] = rotr32(v[b] ^ v[c], 7);       \
} while (0)

static void compress(uint32_t h[8], const uint8_t block[64], uint64_t t, int last) {
    uint32_t v[16], m[16];
    for (int i = 0; i < 16; i++) m[i] = load32le(block + 4 * i);
    for (int i = 0; i < 8; i++)  v[i] = h[i];
    for (int i = 0; i < 8; i++)  v[8 + i] = IV[i];
    v[12] ^= (uint32_t)t;
    v[13] ^= (uint32_t)(t >> 32);
    if (last) v[14] = ~v[14];

    for (int r = 0; r < 10; r++) {
        const uint8_t* s = SIGMA[r];
        G(0, 4,  8, 12, m[s[0]],  m[s[1]]);
        G(1, 5,  9, 13, m[s[2]],  m[s[3]]);
        G(2, 6, 10, 14, m[s[4]],  m[s[5]]);
        G(3, 7, 11, 15, m[s[6]],  m[s[7]]);
        G(0, 5, 10, 15, m[s[8]],  m[s[9]]);
        G(1, 6, 11, 12, m[s[10]], m[s[11]]);
        G(2, 7,  8, 13, m[s[12]], m[s[13]]);
        G(3, 4,  9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; i++) h[i] ^= v[i] ^ v[8 + i];
}

// Core: optional key (keylen 0..32) makes this a MAC. A keyed hash prepends the
// key, zero-padded to a full 64-byte block, as the first compressed block.
static void blake2s_core(uint8_t out[32], const uint8_t* key, uint32_t keylen,
                         const uint8_t* in, uint32_t inlen) {
    if (keylen > 32) keylen = 32;
    uint32_t h[8];
    for (int i = 0; i < 8; i++) h[i] = IV[i];
    h[0] ^= 0x01010000 ^ (keylen << 8) ^ 32;  // param: fanout=depth=1, keylen, digest=32

    uint64_t t = 0;
    if (keylen > 0) {                          // keyed: the padded key is block 0
        uint8_t kb[64];
        for (uint32_t i = 0; i < 64; i++) kb[i] = (i < keylen) ? key[i] : 0;
        if (inlen == 0) { compress(h, kb, 64, 1); goto emit; }
        compress(h, kb, 64, 0); t = 64;
    }
    while (inlen > 64) {                        // all but the final message block
        t += 64;
        compress(h, in, t, 0);
        in += 64; inlen -= 64;
    }
    {
        uint8_t last[64];                       // final block, zero-padded
        for (uint32_t i = 0; i < 64; i++) last[i] = (i < inlen) ? in[i] : 0;
        t += inlen;
        compress(h, last, t, 1);
    }
emit:
    for (int i = 0; i < 8; i++) store32le(out + 4 * i, h[i]);
}

void blake2s256(const uint8_t* in, uint32_t inlen, uint8_t out[32]) {
    blake2s_core(out, 0, 0, in, inlen);
}

void blake2s256_keyed(const uint8_t* key, uint32_t keylen,
                      const uint8_t* in, uint32_t inlen, uint8_t out[32]) {
    blake2s_core(out, key, keylen, in, inlen);
}

// ---------------- Known-answer test (reference BLAKE2s-256 vectors) ----------------
static int eq32(const uint8_t* a, const uint8_t* b) { for (int i = 0; i < 32; i++) if (a[i] != b[i]) return 0; return 1; }

int blake2s_selftest(void) {
    uint8_t out[32];
    static const uint8_t EMPTY[32] = {
        0x69,0x21,0x7a,0x30,0x79,0x90,0x80,0x94,0xe1,0x11,0x21,0xd0,0x42,0x35,0x4a,0x7c,
        0x1f,0x55,0xb6,0x48,0x2c,0xa1,0xa5,0x1e,0x1b,0x25,0x0d,0xfd,0x1e,0xd0,0xee,0xf9 };
    static const uint8_t ABC[32] = {
        0x50,0x8c,0x5e,0x8c,0x32,0x7c,0x14,0xe2,0xe1,0xa7,0x2b,0xa3,0x4e,0xeb,0x45,0x2f,
        0x37,0x45,0x8b,0x20,0x9e,0xd6,0x3a,0x29,0x4d,0x99,0x9b,0x4c,0x86,0x67,0x59,0x82 };
    static const uint8_t V64[32] = {   // hash of the 64 bytes 0x00..0x3f (one full block)
        0x56,0xf3,0x4e,0x8b,0x96,0x55,0x7e,0x90,0xc1,0xf2,0x4b,0x52,0xd0,0xc8,0x9d,0x51,
        0x08,0x6a,0xcf,0x1b,0x00,0xf6,0x34,0xcf,0x1d,0xde,0x92,0x33,0xb8,0xea,0xaa,0x3e };
    static const uint8_t FOX[32] = {   // hash of "The quick brown fox jumps over the lazy dog"
        0x60,0x6b,0xee,0xec,0x74,0x3c,0xcb,0xef,0xf6,0xcb,0xcd,0xf5,0xd5,0x30,0x2a,0xa8,
        0x55,0xc2,0x56,0xc2,0x9b,0x88,0xc8,0xed,0x33,0x1e,0xa1,0xa6,0xbf,0x3c,0x88,0x12 };

    blake2s256((const uint8_t*)"", 0, out);    if (!eq32(out, EMPTY)) return 1;
    blake2s256((const uint8_t*)"abc", 3, out); if (!eq32(out, ABC))   return 2;
    uint8_t m64[64]; for (int i = 0; i < 64; i++) m64[i] = (uint8_t)i;
    blake2s256(m64, 64, out); if (!eq32(out, V64)) return 3;          // exercises the block boundary
    blake2s256((const uint8_t*)"The quick brown fox jumps over the lazy dog", 43, out);
    if (!eq32(out, FOX)) return 4;

    // Sensitivity: one flipped input bit must change the digest.
    uint8_t out2[32], m2[64]; for (int i = 0; i < 64; i++) m2[i] = m64[i]; m2[0] ^= 1;
    blake2s256(m2, 64, out2); if (eq32(out2, V64)) return 5;

    // Keyed (MAC) mode — key = 00..1f, reference BLAKE2s keyed KAT vectors.
    uint8_t key[32]; for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    static const uint8_t K0[32] = {   // empty message (first vector of the official keyed KAT)
        0x48,0xa8,0x99,0x7d,0xa4,0x07,0x87,0x6b,0x3d,0x79,0xc0,0xd9,0x23,0x25,0xad,0x3b,
        0x89,0xcb,0xb7,0x54,0xd8,0x6a,0xb7,0x1a,0xee,0x04,0x7a,0xd3,0x45,0xfd,0x2c,0x49 };
    static const uint8_t K1[32] = {
        0x40,0xd1,0x5f,0xee,0x7c,0x32,0x88,0x30,0x16,0x6a,0xc3,0xf9,0x18,0x65,0x0f,0x80,
        0x7e,0x7e,0x01,0xe1,0x77,0x25,0x8c,0xdc,0x0a,0x39,0xb1,0x1f,0x59,0x80,0x66,0xf1 };
    static const uint8_t K64V[32] = {
        0x89,0x75,0xb0,0x57,0x7f,0xd3,0x55,0x66,0xd7,0x50,0xb3,0x62,0xb0,0x89,0x7a,0x26,
        0xc3,0x99,0x13,0x6d,0xf0,0x7b,0xab,0xab,0xbd,0xe6,0x20,0x3f,0xf2,0x95,0x4e,0xd4 };
    static const uint8_t K100[32] = {
        0xc3,0x76,0x61,0x70,0x14,0xd2,0x01,0x58,0xbc,0xed,0x3d,0x3b,0xa5,0x52,0xb6,0xec,
        0xcf,0x84,0xe6,0x2a,0xa3,0xeb,0x65,0x0e,0x90,0x02,0x9c,0x84,0xd1,0x3e,0xea,0x69 };
    blake2s256_keyed(key, 32, (const uint8_t*)"", 0, out); if (!eq32(out, K0))   return 6;
    uint8_t one = 0;
    blake2s256_keyed(key, 32, &one, 1, out);               if (!eq32(out, K1))   return 7;
    blake2s256_keyed(key, 32, m64, 64, out);               if (!eq32(out, K64V)) return 8;
    uint8_t m100[100]; for (int i = 0; i < 100; i++) m100[i] = (uint8_t)i;
    blake2s256_keyed(key, 32, m100, 100, out);             if (!eq32(out, K100)) return 9;
    return 0;
}
