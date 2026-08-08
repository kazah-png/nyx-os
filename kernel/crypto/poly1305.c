// ============================================================
// Poly1305 one-time authenticator (RFC 8439) — the "poly1305-donna" 32-bit
// formulation: the 130-bit accumulator is held as five 26-bit limbs so every
// partial product fits in a uint64 — no 128-bit division (which the freestanding
// link has no libgcc helper for), only 32x32->64 multiplies.
// ============================================================

#include "poly1305.h"

static uint32_t u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void u32to8(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

typedef struct {
    uint32_t r0, r1, r2, r3, r4;      // clamped key r, in 26-bit limbs
    uint32_t s1, s2, s3, s4;          // r1..r4 * 5 (for the mod-2^130-5 fold)
    uint32_t h0, h1, h2, h3, h4;      // accumulator
    uint32_t pad0, pad1, pad2, pad3;  // key s (added at the end)
} poly_st;

// Absorb one 16-byte block (bytes already in `b`), with hibit = 1<<24 for a full
// block (the implicit high 1 bit), or 0 for the final short block (whose 1 bit is
// already set in the byte buffer).
static void poly_block(poly_st* st, const uint8_t* b, uint32_t hibit) {
    uint32_t b0 = u32le(b), b1 = u32le(b + 4), b2 = u32le(b + 8), b3 = u32le(b + 12);
    st->h0 += ( b0                     ) & 0x3ffffff;
    st->h1 += ((b0 >> 26) | (b1 <<  6) ) & 0x3ffffff;
    st->h2 += ((b1 >> 20) | (b2 << 12) ) & 0x3ffffff;
    st->h3 += ((b2 >> 14) | (b3 << 18) ) & 0x3ffffff;
    st->h4 += ((b3 >>  8)              ) | hibit;

    uint64_t d0 = (uint64_t)st->h0 * st->r0 + (uint64_t)st->h1 * st->s4 + (uint64_t)st->h2 * st->s3 + (uint64_t)st->h3 * st->s2 + (uint64_t)st->h4 * st->s1;
    uint64_t d1 = (uint64_t)st->h0 * st->r1 + (uint64_t)st->h1 * st->r0 + (uint64_t)st->h2 * st->s4 + (uint64_t)st->h3 * st->s3 + (uint64_t)st->h4 * st->s2;
    uint64_t d2 = (uint64_t)st->h0 * st->r2 + (uint64_t)st->h1 * st->r1 + (uint64_t)st->h2 * st->r0 + (uint64_t)st->h3 * st->s4 + (uint64_t)st->h4 * st->s3;
    uint64_t d3 = (uint64_t)st->h0 * st->r3 + (uint64_t)st->h1 * st->r2 + (uint64_t)st->h2 * st->r1 + (uint64_t)st->h3 * st->r0 + (uint64_t)st->h4 * st->s4;
    uint64_t d4 = (uint64_t)st->h0 * st->r4 + (uint64_t)st->h1 * st->r3 + (uint64_t)st->h2 * st->r2 + (uint64_t)st->h3 * st->r1 + (uint64_t)st->h4 * st->r0;

    uint32_t c;
    c = (uint32_t)(d0 >> 26); st->h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); st->h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); st->h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); st->h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); st->h4 = (uint32_t)d4 & 0x3ffffff; st->h0 += c * 5;
    c = st->h0 >> 26;         st->h0 = st->h0 & 0x3ffffff;       st->h1 += c;
}

void poly1305_mac(const uint8_t key[32], const uint8_t* msg, uint32_t len, uint8_t tag[16]) {
    poly_st st;
    uint32_t t0 = u32le(key), t1 = u32le(key + 4), t2 = u32le(key + 8), t3 = u32le(key + 12);
    st.r0 = ( t0                    ) & 0x3ffffff;
    st.r1 = ((t0 >> 26) | (t1 <<  6)) & 0x3ffff03;
    st.r2 = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
    st.r3 = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
    st.r4 = ((t3 >>  8)             ) & 0x00fffff;
    st.s1 = st.r1 * 5; st.s2 = st.r2 * 5; st.s3 = st.r3 * 5; st.s4 = st.r4 * 5;
    st.h0 = st.h1 = st.h2 = st.h3 = st.h4 = 0;
    st.pad0 = u32le(key + 16); st.pad1 = u32le(key + 20); st.pad2 = u32le(key + 24); st.pad3 = u32le(key + 28);

    while (len >= 16) { poly_block(&st, msg, 1u << 24); msg += 16; len -= 16; }
    if (len) {
        uint8_t b[16];
        for (uint32_t i = 0; i < 16; i++) b[i] = (i < len) ? msg[i] : 0;
        b[len] = 1;                             // the implicit high 1 bit
        poly_block(&st, b, 0);
    }

    uint32_t h0 = st.h0, h1 = st.h1, h2 = st.h2, h3 = st.h3, h4 = st.h4, c;
    // fully propagate carries
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    // g = h - (2^130 - 5)
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1u << 26);
    // if h < p keep h (mask 0), else take g (mask all-ones)
    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
    // reduce to 4 x 32-bit words (mod 2^128)
    h0 = ((h0      ) | (h1 << 26));
    h1 = ((h1 >>  6) | (h2 << 20));
    h2 = ((h2 >> 12) | (h3 << 14));
    h3 = ((h3 >> 18) | (h4 <<  8));
    // tag = (h + s) mod 2^128
    uint64_t f;
    f = (uint64_t)h0 + st.pad0;              h0 = (uint32_t)f;
    f = (uint64_t)h1 + st.pad1 + (f >> 32);  h1 = (uint32_t)f;
    f = (uint64_t)h2 + st.pad2 + (f >> 32);  h2 = (uint32_t)f;
    f = (uint64_t)h3 + st.pad3 + (f >> 32);  h3 = (uint32_t)f;
    u32to8(tag + 0, h0); u32to8(tag + 4, h1); u32to8(tag + 8, h2); u32to8(tag + 12, h3);
}

// ---------------- Known-answer test ----------------
static int pl_eq(const uint8_t* a, const uint8_t* b) { for (int i = 0; i < 16; i++) if (a[i] != b[i]) return 0; return 1; }

int poly1305_selftest(void) {
    uint8_t tag[16];

    // RFC 8439 §2.5.2
    static const uint8_t K1[32] = {
        0x85,0xd6,0xbe,0x78,0x57,0x55,0x6d,0x33,0x7f,0x44,0x52,0xfe,0x42,0xd5,0x06,0xa8,
        0x01,0x03,0x80,0x8a,0xfb,0x0d,0xb2,0xfd,0x4a,0xbf,0xf6,0xaf,0x41,0x49,0xf5,0x1b };
    static const uint8_t V1[16] = { 0xa8,0x06,0x1d,0xc1,0x30,0x51,0x36,0xc6,0xc2,0x2b,0x8b,0xaf,0x0c,0x01,0x27,0xa9 };
    poly1305_mac(K1, (const uint8_t*)"Cryptographic Forum Research Group", 34, tag);
    if (!pl_eq(tag, V1)) return 1;

    // key = 00 01 … 1f, three message lengths (0 / 16 / 37) — vectors from a reference impl
    uint8_t K2[32]; for (int i = 0; i < 32; i++) K2[i] = (uint8_t)i;
    static const uint8_t V2[16] = { 0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f };
    poly1305_mac(K2, (const uint8_t*)"", 0, tag);
    if (!pl_eq(tag, V2)) return 2;

    static const uint8_t V3[16] = { 0xa2,0x29,0x1a,0x36,0x3d,0xef,0x0b,0x53,0x84,0x5f,0xa4,0x12,0x6a,0x6a,0xd3,0x64 };
    uint8_t m16[16]; for (int i = 0; i < 16; i++) m16[i] = (uint8_t)i;
    poly1305_mac(K2, m16, 16, tag);
    if (!pl_eq(tag, V3)) return 3;

    static const uint8_t V4[16] = { 0x8f,0xf7,0xdf,0x6f,0x96,0x23,0x47,0x39,0x6b,0x37,0x0b,0x48,0xd1,0x50,0xc5,0xe7 };
    poly1305_mac(K2, (const uint8_t*)"NyxOS-Poly1305-test-vector-0123456789", 37, tag);
    if (!pl_eq(tag, V4)) return 4;

    // Sensitivity: a one-bit key change must change the tag.
    uint8_t tag2[16], K3[32]; for (int i = 0; i < 32; i++) K3[i] = K2[i]; K3[0] ^= 1;
    poly1305_mac(K3, m16, 16, tag2);
    if (pl_eq(tag2, V3)) return 5;
    return 0;
}
