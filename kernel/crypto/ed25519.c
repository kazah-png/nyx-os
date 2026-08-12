// ============================================================
// ed25519.c - Ed25519 (RFC 8032) public-key derivation + KAT (see ed25519.h).
// The field and point arithmetic follow the public-domain TweetNaCl reference
// (a field element is 16 little-endian limbs of ~16 bits in signed 64-bit words).
// ============================================================
#include "ed25519.h"
#include "sha512.h"

typedef int64_t gf[16];

static const gf gf0 = {0};
static const gf gf1 = {1};
// D2 = 2*d, and the Edwards base point (X, Y) — the standard TweetNaCl constants.
static const gf D2 = {0xf159,0x26b2,0x9b94,0xebd6,0xb156,0x8283,0x149a,0x00e0,
                      0xd130,0xeef3,0x80f2,0x198e,0xfce7,0x56df,0xd9dc,0x2406};
static const gf X  = {0xd51a,0x8f25,0x2d60,0xc956,0xa7b2,0x9525,0xc760,0x692c,
                      0xdc5c,0xfdd6,0xe231,0xc0a4,0x53fe,0xcd6e,0x36d3,0x2169};
static const gf Y  = {0x6658,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,
                      0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666,0x6666};

static void fe_set(gf r, const gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }

static void fe_carry(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void fe_sel(gf p, gf q, int b) {
    int64_t t, c = ~((int64_t)b - 1);
    for (int i = 0; i < 16; i++) { t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}

static void fe_pack(uint8_t* o, const gf n) {
    gf m, t; int b;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    fe_carry(t); fe_carry(t); fe_carry(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) { m[i] = t[i] - 0xffff - ((m[i-1] >> 16) & 1); m[i-1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (m[15] >> 16) & 1; m[14] &= 0xffff;
        fe_sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) { o[2*i] = t[i] & 0xff; o[2*i+1] = t[i] >> 8; }
}

static void fe_add(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fe_sub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void fe_mul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i+j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i+16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    fe_carry(o); fe_carry(o);
}

static void fe_sq(gf o, const gf a) { fe_mul(o, a, a); }

static void fe_inv(gf o, const gf in) {
    gf c; for (int a = 0; a < 16; a++) c[a] = in[a];
    for (int a = 253; a >= 0; a--) { fe_sq(c, c); if (a != 2 && a != 4) fe_mul(c, c, in); }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static uint8_t fe_par(const gf a) { uint8_t d[32]; fe_pack(d, a); return d[0] & 1; }

// A point in extended twisted-Edwards coordinates: p[0..3] = (X, Y, Z, T).
static void pt_add(gf p[4], gf q[4]) {
    gf a, b, c, d, e, f, g, h, t;
    fe_sub(a, p[1], p[0]); fe_sub(t, q[1], q[0]); fe_mul(a, a, t);
    fe_add(b, p[0], p[1]); fe_add(t, q[0], q[1]); fe_mul(b, b, t);
    fe_mul(c, p[3], q[3]); fe_mul(c, c, D2);
    fe_mul(d, p[2], q[2]); fe_add(d, d, d);
    fe_sub(e, b, a); fe_sub(f, d, c); fe_add(g, d, c); fe_add(h, b, a);
    fe_mul(p[0], e, f); fe_mul(p[1], h, g); fe_mul(p[2], g, f); fe_mul(p[3], e, h);
}

static void pt_cswap(gf p[4], gf q[4], uint8_t b) { for (int i = 0; i < 4; i++) fe_sel(p[i], q[i], b); }

static void pt_pack(uint8_t* r, gf p[4]) {
    gf tx, ty, zi;
    fe_inv(zi, p[2]);
    fe_mul(tx, p[0], zi);
    fe_mul(ty, p[1], zi);
    fe_pack(r, ty);
    r[31] ^= fe_par(tx) << 7;      // stash the sign of x in the top bit of the y encoding
}

static void pt_scalarmult(gf p[4], gf q[4], const uint8_t* s) {
    fe_set(p[0], gf0); fe_set(p[1], gf1); fe_set(p[2], gf1); fe_set(p[3], gf0);   // neutral
    for (int i = 255; i >= 0; --i) {
        uint8_t b = (s[i >> 3] >> (i & 7)) & 1;
        pt_cswap(p, q, b); pt_add(q, p); pt_add(p, p); pt_cswap(p, q, b);
    }
}

static void pt_scalarbase(gf p[4], const uint8_t* s) {
    gf q[4];
    fe_set(q[0], X); fe_set(q[1], Y); fe_set(q[2], gf1); fe_mul(q[3], X, Y);
    pt_scalarmult(p, q, s);
}

void ed25519_pubkey(const uint8_t seed[32], uint8_t pk[32]) {
    uint8_t h[64]; gf p[4];
    sha512(seed, 32, h);
    h[0] &= 248; h[31] &= 127; h[31] |= 64;      // clamp the scalar (RFC 8032 §5.1.5)
    pt_scalarbase(p, h);
    pt_pack(pk, p);
}

// ---- known-answer self-test (`ed25519pub`) ----
// Golden (seed -> public key) pair generated with the RFC 8032 reference (Python
// `cryptography`): seed = 00,01,02,...,1f.
int ed25519_selftest(void) {
    static const uint8_t seed[32] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31 };
    static const uint8_t want[32] = {
        0x03,0xa1,0x07,0xbf,0xf3,0xce,0x10,0xbe,0x1d,0x70,0xdd,0x18,0xe7,0x4b,0xc0,0x99,
        0x67,0xe4,0xd6,0x30,0x9b,0xa5,0x0d,0x5f,0x1d,0xdc,0x86,0x64,0x12,0x55,0x31,0xb8 };
    uint8_t pk[32];
    ed25519_pubkey(seed, pk);
    for (int i = 0; i < 32; i++) if (pk[i] != want[i]) return 1;
    return 0;
}
