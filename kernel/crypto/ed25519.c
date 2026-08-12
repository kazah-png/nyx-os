// ============================================================
// ed25519.c - Ed25519 (RFC 8032) public-key derivation + KAT (see ed25519.h).
// The field and point arithmetic follow the public-domain TweetNaCl reference
// (a field element is 16 little-endian limbs of ~16 bits in signed 64-bit words).
// ============================================================
#include "ed25519.h"
#include "sha512.h"
#include "ct.h"

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
// d = -121665/121666 (D2 above is 2*d) and I = sqrt(-1) mod p — used only by the
// verify path's point decompression (recovering x from y).
static const gf D  = {0x78a3,0x1359,0x4dca,0x75eb,0xd8ab,0x4141,0x0a4d,0x0070,
                      0xe898,0x7779,0x4079,0x8cc7,0xfe73,0x2b6f,0x6cee,0x5203};
static const gf I  = {0xa0b0,0x4a0e,0x1b27,0xc4ee,0xe478,0xad2f,0x1806,0x2f43,
                      0xd7a7,0x3dfb,0x0099,0x2b4d,0xdf0b,0x4fc1,0x2480,0x2b83};

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

// o = in^((p-5)/8): the exponent that yields a square root candidate mod p (verify only).
static void fe_pow2523(gf o, const gf in) {
    gf c; for (int a = 0; a < 16; a++) c[a] = in[a];
    for (int a = 250; a >= 0; a--) { fe_sq(c, c); if (a != 1) fe_mul(c, c, in); }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static uint8_t fe_par(const gf a) { uint8_t d[32]; fe_pack(d, a); return d[0] & 1; }

// Decode 32 little-endian bytes into a field element (top bit dropped) — inverse of
// fe_pack, used to decompress a public key / R point during verify.
static void fe_unpack(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; i++) o[i] = n[2*i] + ((int64_t)n[2*i+1] << 8);
    o[15] &= 0x7fff;
}

// Constant-time field-element inequality: nonzero when the packed forms differ.
static int fe_neq(const gf a, const gf b) {
    uint8_t c[32], d[32];
    fe_pack(c, a); fe_pack(d, b);
    return ct_memcmp(c, d, 32);
}

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

// Decompress a 32-byte point encoding into -P in extended coords (TweetNaCl's
// unpackneg): recover x from y via the curve equation and a mod-p square root, fix its
// sign from the encoded parity bit, then negate (verify computes [S]B + [h](-A)).
// Returns -1 if the bytes are not a valid curve point.
static int pt_unpackneg(gf r[4], const uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    fe_set(r[2], gf1);
    fe_unpack(r[1], p);
    fe_sq(num, r[1]);
    fe_mul(den, num, D);
    fe_sub(num, num, r[2]);
    fe_add(den, r[2], den);
    fe_sq(den2, den);
    fe_sq(den4, den2);
    fe_mul(den6, den4, den2);
    fe_mul(t, den6, num);
    fe_mul(t, t, den);
    fe_pow2523(t, t);
    fe_mul(t, t, num);
    fe_mul(t, t, den);
    fe_mul(t, t, den);
    fe_mul(r[0], t, den);
    fe_sq(chk, r[0]);
    fe_mul(chk, chk, den);
    if (fe_neq(chk, num)) fe_mul(r[0], r[0], I);   // wrong root: multiply by sqrt(-1)
    fe_sq(chk, r[0]);
    fe_mul(chk, chk, den);
    if (fe_neq(chk, num)) return -1;               // still wrong: not on the curve
    if (fe_par(r[0]) == (p[31] >> 7)) fe_sub(r[0], gf0, r[0]);   // pick the -x sign
    fe_mul(r[3], r[0], r[1]);
    return 0;
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

// ---- scalar arithmetic mod L (the base-point group order) ----
// L = 2^252 + 27742317777372353535851937790883648493, little-endian.
static const int64_t ED_L[32] = {
    0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10 };

// Reduce the 64-limb value x mod L into 32 bytes r (TweetNaCl's modL, verbatim shape).
static void sc_modL(uint8_t* r, int64_t x[64]) {
    int64_t carry; int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * ED_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * ED_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) x[j] -= carry * ED_L[j];
    for (i = 0; i < 32; ++i) {
        x[i+1] += x[i] >> 8;
        r[i] = x[i] & 255;
    }
}

// In-place reduce the 64-byte little-endian h mod L; the reduced 32-byte scalar lands
// in h[0..31] and h[32..63] is cleared.
static void sc_reduce(uint8_t* h) {
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = (int64_t)(uint64_t)h[i];
    for (int i = 0; i < 64; i++) h[i] = 0;
    sc_modL(h, x);
}

// Verify a 64-byte detached signature sig = R(32)||S(32) over msg under public key pk.
// Returns 0 iff the signature is valid: with h = SHA-512(R‖A‖M) mod L, it checks
// [S]B == R + [h]A by testing pack([S]B + [h](-A)) == R. Constant-time final compare.
int ed25519_verify(const uint8_t sig[64], const uint8_t* msg, uint32_t mlen, const uint8_t pk[32]) {
    gf p[4], q[4];
    uint8_t h[64], t[32];
    sha512_ctx_t c;
    if (pt_unpackneg(q, pk)) return -1;          // q = -A (also rejects a bad key encoding)
    sha512_init(&c);
    sha512_update(&c, sig, 32);                  // R
    sha512_update(&c, pk, 32);                   // A
    sha512_update(&c, msg, mlen);                // M
    sha512_final(&c, h);
    sc_reduce(h);                                // h mod L, in h[0..31]
    pt_scalarmult(p, q, h);                      // [h](-A)
    pt_scalarbase(q, sig + 32);                  // [S]B
    pt_add(p, q);                                // [S]B + [h](-A)  == R iff valid
    pt_pack(t, p);
    return ct_memcmp(sig, t, 32) == 0 ? 0 : -1;
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

// KAT for ed25519_verify: RFC 8032 §7.1 test vector 2 (public key, 1-byte message
// 0x72, signature). The genuine signature must verify; a single flipped signature bit
// and a flipped public-key bit must both be rejected.
int ed25519_verify_selftest(void) {
    static const uint8_t pk[32] = {
        0x3d,0x40,0x17,0xc3,0xe8,0x43,0x89,0x5a,0x92,0xb7,0x0a,0xa7,0x4d,0x1b,0x7e,0xbc,
        0x9c,0x98,0x2c,0xcf,0x2e,0xc4,0x96,0x8c,0xc0,0xcd,0x55,0xf1,0x2a,0xf4,0x66,0x0c };
    static const uint8_t msg[1] = { 0x72 };
    static const uint8_t sig[64] = {
        0x92,0xa0,0x09,0xa9,0xf0,0xd4,0xca,0xb8,0x72,0x0e,0x82,0x0b,0x5f,0x64,0x25,0x40,
        0xa2,0xb2,0x7b,0x54,0x16,0x50,0x3f,0x8f,0xb3,0x76,0x22,0x23,0xeb,0xdb,0x69,0xda,
        0x08,0x5a,0xc1,0xe4,0x3e,0x15,0x99,0x6e,0x45,0x8f,0x36,0x13,0xd0,0xf1,0x1d,0x8c,
        0x38,0x7b,0x2e,0xae,0xb4,0x30,0x2a,0xee,0xb0,0x0d,0x29,0x16,0x12,0xbb,0x0c,0x00 };
    if (ed25519_verify(sig, msg, 1, pk) != 0) return 1;      // genuine signature must pass
    uint8_t bad[64];
    for (int i = 0; i < 64; i++) bad[i] = sig[i];
    bad[40] ^= 0x01;                                         // flip a bit in S
    if (ed25519_verify(bad, msg, 1, pk) == 0) return 2;      // tampered sig must be rejected
    uint8_t badpk[32];
    for (int i = 0; i < 32; i++) badpk[i] = pk[i];
    badpk[0] ^= 0x01;
    if (ed25519_verify(sig, msg, 1, badpk) == 0) return 3;   // wrong key must be rejected
    return 0;
}
