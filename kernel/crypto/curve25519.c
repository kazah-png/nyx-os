// ============================================================
// curve25519.c - X25519 (Curve25519 ECDH), v5.9.49
// ============================================================
// The ephemeral Diffie-Hellman function every modern TLS handshake uses:
// example.com negotiated ECDHE (cipher 0xc02b) and offered an X25519 key, so
// this is the first cryptographic primitive on the road to reading an https page.
// It implements X25519 exactly as RFC 7748 specifies — the Montgomery ladder over
// the prime field GF(2^255-19) — and is constant-time (no secret-dependent
// branches or memory indexing). Correctness is pinned by the RFC 7748 known-answer
// vectors in curve25519_selftest(), reachable from the `x25519test` command.
//
// The field arithmetic follows the well-known TweetNaCl reference (public domain):
// a field element is 16 limbs of ~16 bits held in signed 64-bit words, so limb
// products can never overflow. Only +, -, *, shifts and bitwise ops are used — no
// division — which suits a freestanding kernel with no libgcc soft-arithmetic.
#include "../core/kernel.h"
#include "curve25519.h"

typedef int64_t gf[16];   // field element: 16 little-endian limbs, radix 2^16

// The Montgomery curve constant a24 = (486662 - 2) / 4 = 121665.
static const gf gf_121665 = { 0xDB41, 1 };

static void fe_copy(gf r, const gf a) {
    for (int i = 0; i < 16; i++) r[i] = a[i];
}

// Constant-time conditional swap of a and b, performed iff swap == 1.
static void fe_cswap(gf a, gf b, int64_t swap) {
    int64_t mask = ~(swap - 1);          // all-zeros if swap==0, all-ones if swap==1
    for (int i = 0; i < 16; i++) {
        int64_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

// Carry-propagate limbs back toward [0, 2^16); the top carry folds in via 2^256 ≡ 38 (mod p).
static void fe_carry(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void fe_add(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void fe_sub(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void fe_mul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];   // fold the high half: 2^256 ≡ 38
    for (int i = 0; i < 16; i++) o[i] = t[i];
    fe_carry(o);
    fe_carry(o);
}

static void fe_sq(gf o, const gf a) {
    fe_mul(o, a, a);
}

// Multiplicative inverse via Fermat's little theorem: a^(p-2) = a^(2^255 - 21) (mod p).
static void fe_inv(gf o, const gf a) {
    gf c;
    fe_copy(c, a);
    for (int i = 253; i >= 0; i--) {
        fe_sq(c, c);
        if (i != 2 && i != 4) fe_mul(c, c, a);
    }
    fe_copy(o, c);
}

// Decode 32 little-endian bytes into a field element (top bit is cleared per RFC 7748).
static void fe_unpack(gf o, const uint8_t* n) {
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

// Fully reduce mod p (two conditional subtractions of p) and serialize to 32 bytes.
static void fe_pack(uint8_t* o, const gf n) {
    gf t, m;
    fe_copy(t, n);
    fe_carry(t); fe_carry(t); fe_carry(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        fe_cswap(t, m, 1 - b);      // keep the reduced value when it did not underflow
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i]     = t[i] & 0xff;
        o[2 * i + 1] = (t[i] >> 8) & 0xff;
    }
}

// X25519: out = scalar * point, computed with the Montgomery ladder on the u-coordinate.
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t e[32];
    for (int i = 0; i < 32; i++) e[i] = scalar[i];
    e[0]  &= 248;                    // clamp: clear the low 3 bits
    e[31]  = (e[31] & 127) | 64;     // clamp: clear the top bit, set bit 254

    gf x, a, b, c, d, ee, f;
    fe_unpack(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; a[i] = c[i] = d[i] = 0; }
    a[0] = d[0] = 1;                 // (x2,z2) = (1,0), (x3,z3) = (u,1)

    for (int i = 254; i >= 0; i--) {
        int64_t bit = (e[i >> 3] >> (i & 7)) & 1;
        fe_cswap(a, b, bit);
        fe_cswap(c, d, bit);
        fe_add(ee, a, c);
        fe_sub(a, a, c);
        fe_add(c, b, d);
        fe_sub(b, b, d);
        fe_sq(d, ee);
        fe_sq(f, a);
        fe_mul(a, c, a);
        fe_mul(c, b, ee);
        fe_add(ee, a, c);
        fe_sub(a, a, c);
        fe_sq(b, a);
        fe_sub(c, d, f);
        fe_mul(a, c, gf_121665);
        fe_add(a, a, d);
        fe_mul(c, c, a);
        fe_mul(a, d, f);
        fe_mul(d, b, x);
        fe_sq(b, ee);
        fe_cswap(a, b, bit);
        fe_cswap(c, d, bit);
    }
    // out = x2 / z2 = a * c^-1
    fe_inv(c, c);
    fe_mul(a, a, c);
    fe_pack(out, a);
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    static const uint8_t base[32] = { 9 };   // the Curve25519 base point, u = 9
    x25519(out, scalar, base);
}

// ---- RFC 7748 known-answer self-test ------------------------------------------------

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void hex32(uint8_t out[32], const char* h) {
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)((hexval(h[2 * i]) << 4) | hexval(h[2 * i + 1]));
}
static int eq32(const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 32; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int curve25519_selftest(void) {
    uint8_t k[32], u[32], want[32], got[32];
    int pass = 0, total = 0;

    // RFC 7748 §5.2, test vector 1.
    hex32(k,    "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
    hex32(u,    "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
    hex32(want, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
    x25519(got, k, u); total++;
    if (eq32(got, want)) { pass++; printf("x25519: KAT1 scalarmult PASS\n"); }
    else                   printf("x25519: KAT1 scalarmult FAIL\n");

    // RFC 7748 §5.2, test vector 2.
    hex32(k,    "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d");
    hex32(u,    "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493");
    hex32(want, "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957");
    x25519(got, k, u); total++;
    if (eq32(got, want)) { pass++; printf("x25519: KAT2 scalarmult PASS\n"); }
    else                   printf("x25519: KAT2 scalarmult FAIL\n");

    // RFC 7748 §6.1, the Diffie-Hellman worked example.
    uint8_t apriv[32], apub[32], bpriv[32], bpub[32], ss1[32], ss2[32];
    hex32(apriv, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    hex32(bpriv, "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");

    hex32(want, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    x25519_base(apub, apriv); total++;
    if (eq32(apub, want)) { pass++; printf("x25519: Alice public key PASS\n"); }
    else                    printf("x25519: Alice public key FAIL\n");

    hex32(want, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    x25519_base(bpub, bpriv); total++;
    if (eq32(bpub, want)) { pass++; printf("x25519: Bob public key PASS\n"); }
    else                    printf("x25519: Bob public key FAIL\n");

    hex32(want, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    x25519(ss1, apriv, bpub);      // Alice computes with Bob's public key
    x25519(ss2, bpriv, apub);      // Bob computes with Alice's public key
    total++;
    if (eq32(ss1, want) && eq32(ss2, want)) { pass++; printf("x25519: ECDH shared secret PASS (both sides agree)\n"); }
    else                                      printf("x25519: ECDH shared secret FAIL\n");

    printf("x25519: self-test %d/%d vectors passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
