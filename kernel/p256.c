// ============================================================
// p256.c - NIST P-256 (secp256r1) field + point arithmetic, v5.9.59
// ============================================================
// The curve the server's certificate signs with: cipher 0xc02b is ECDSA-P256, so verifying
// the ServerKeyExchange signature needs P-256 point arithmetic under the cert's public key.
// This provides GF(p) arithmetic (Montgomery multiplication, 4x64-bit limbs, native __int128
// products) and point add / double / scalar-multiply in Jacobian coordinates (a = -3), plus
// affine I/O. Pinned by `p256test` against NIST base-point multiples. ECDSA verify (the mod-n
// scalar side + the verification equation) is the next increment.
#include "kernel.h"
#include "p256.h"

typedef uint64_t fe[4];                 // field element: 4 little-endian 64-bit limbs

// P-256 prime p and R^2 mod p (for Montgomery conversion). n0' = -p^-1 mod 2^64 = 1.
static const uint64_t Pp[4] = { 0xffffffffffffffffULL, 0x00000000ffffffffULL,
                                0x0000000000000000ULL, 0xffffffff00000001ULL };
static const fe FE_R2       = { 0x0000000000000003ULL, 0xfffffffbffffffffULL,
                                0xfffffffffffffffeULL, 0x00000004fffffffdULL };

static void fe_copy(fe r, const fe a) { for (int i = 0; i < 4; i++) r[i] = a[i]; }
static int  fe_is_zero(const fe a)    { return (a[0] | a[1] | a[2] | a[3]) == 0; }
static int  fe_eq(const fe a, const fe b) {
    return ((a[0]^b[0]) | (a[1]^b[1]) | (a[2]^b[2]) | (a[3]^b[3])) == 0;
}

static void fe_add(fe r, const fe a, const fe b) {
    uint64_t t[4]; unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) { c += (unsigned __int128)a[i] + b[i]; t[i] = (uint64_t)c; c >>= 64; }
    uint64_t carry = (uint64_t)c;
    uint64_t s[4]; unsigned __int128 brw = 0;
    for (int i = 0; i < 4; i++) { unsigned __int128 d = (unsigned __int128)t[i] - Pp[i] - brw; s[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (carry | (brw ^ 1));            // subtract p iff sum >= p
    for (int i = 0; i < 4; i++) r[i] = (s[i] & mask) | (t[i] & ~mask);
}

static void fe_sub(fe r, const fe a, const fe b) {
    uint64_t t[4]; unsigned __int128 brw = 0;
    for (int i = 0; i < 4; i++) { unsigned __int128 d = (unsigned __int128)a[i] - b[i] - brw; t[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (uint64_t)brw;                  // add p back on borrow
    unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) { c += (unsigned __int128)t[i] + (Pp[i] & mask); r[i] = (uint64_t)c; c >>= 64; }
}

// Montgomery multiply: r = a*b*R^-1 mod p (CIOS, n0'=1).
static void mont_mul(fe r, const fe a, const fe b) {
    uint64_t t[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        unsigned __int128 C = 0;
        for (int j = 0; j < 4; j++) {
            unsigned __int128 s = (unsigned __int128)t[j] + (unsigned __int128)a[j] * b[i] + C;
            t[j] = (uint64_t)s; C = s >> 64;
        }
        unsigned __int128 s = (unsigned __int128)t[4] + C; t[4] = (uint64_t)s; t[5] = (uint64_t)(s >> 64);
        uint64_t m = t[0];                              // * n0' (=1)
        C = (unsigned __int128)t[0] + (unsigned __int128)m * Pp[0]; C >>= 64;   // low limb cancels
        for (int j = 1; j < 4; j++) {
            unsigned __int128 s2 = (unsigned __int128)t[j] + (unsigned __int128)m * Pp[j] + C;
            t[j-1] = (uint64_t)s2; C = s2 >> 64;
        }
        unsigned __int128 s3 = (unsigned __int128)t[4] + C; t[3] = (uint64_t)s3; C = s3 >> 64;
        t[4] = t[5] + (uint64_t)C;
    }
    uint64_t s[4]; unsigned __int128 brw = 0;
    for (int i = 0; i < 4; i++) { unsigned __int128 d = (unsigned __int128)t[i] - Pp[i] - brw; s[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (t[4] | (brw ^ 1));
    for (int i = 0; i < 4; i++) r[i] = (s[i] & mask) | (t[i] & ~mask);
}

static void fe_sqr(fe r, const fe a)      { mont_mul(r, a, a); }
static void fe_to_mont(fe r, const fe a)  { mont_mul(r, a, FE_R2); }         // a -> a*R
static void fe_from_mont(fe r, const fe a){ fe o = {1,0,0,0}; mont_mul(r, a, o); }   // a*R -> a
static void fe_one_mont(fe r)             { fe o = {1,0,0,0}; fe_to_mont(r, o); }

// r = a^-1 mod p  (Fermat: a^(p-2)), all in Montgomery form.
static void fe_inv(fe r, const fe a) {
    static const uint64_t E[4] = { 0xfffffffffffffffdULL, 0x00000000ffffffffULL,
                                   0x0000000000000000ULL, 0xffffffff00000001ULL };   // p - 2
    fe result; fe_one_mont(result);
    for (int i = 255; i >= 0; i--) {
        fe_sqr(result, result);
        if ((E[i >> 6] >> (i & 63)) & 1) mont_mul(result, result, a);
    }
    fe_copy(r, result);
}

// ---- points in Jacobian coordinates (X:Y:Z), field elements in Montgomery form ----------
typedef struct { fe X, Y, Z; } jac;

static void jac_set_id(jac* p) { fe_one_mont(p->X); fe_one_mont(p->Y); for (int i=0;i<4;i++) p->Z[i]=0; }
static int  jac_is_id(const jac* p) { return fe_is_zero(p->Z); }

// out = 2*a  (dbl-2001-b, a = -3). Safe for out == a.
static void jac_double(jac* out, const jac* a) {
    fe delta, gamma, beta, t1, t2, alpha, X3, Y3, Z3, b2, b4, b8, g2, tmp;
    fe_sqr(delta, a->Z);
    fe_sqr(gamma, a->Y);
    mont_mul(beta, a->X, gamma);
    fe_sub(t1, a->X, delta);
    fe_add(t2, a->X, delta);
    mont_mul(tmp, t1, t2);
    fe_add(alpha, tmp, tmp); fe_add(alpha, alpha, tmp);         // 3*(X-d)*(X+d)
    fe_add(b2, beta, beta); fe_add(b4, b2, b2); fe_add(b8, b4, b4);
    fe_sqr(X3, alpha); fe_sub(X3, X3, b8);                      // alpha^2 - 8*beta
    fe_add(tmp, a->Y, a->Z); fe_sqr(tmp, tmp);
    fe_sub(tmp, tmp, gamma); fe_sub(Z3, tmp, delta);           // (Y+Z)^2 - gamma - delta
    fe_sub(tmp, b4, X3); mont_mul(Y3, alpha, tmp);             // alpha*(4*beta - X3)
    fe_sqr(g2, gamma); fe_add(g2, g2, g2); fe_add(g2, g2, g2); fe_add(g2, g2, g2);  // 8*gamma^2
    fe_sub(Y3, Y3, g2);
    fe_copy(out->X, X3); fe_copy(out->Y, Y3); fe_copy(out->Z, Z3);
}

// out = a + b (add-2007-bl). Handles identity and equal/opposite points. Safe for out == a.
static void jac_add(jac* out, const jac* a, const jac* b) {
    if (jac_is_id(a)) { fe_copy(out->X,b->X); fe_copy(out->Y,b->Y); fe_copy(out->Z,b->Z); return; }
    if (jac_is_id(b)) { fe_copy(out->X,a->X); fe_copy(out->Y,a->Y); fe_copy(out->Z,a->Z); return; }

    fe Z1Z1, Z2Z2, U1, U2, S1, S2, H, rr, t;
    fe_sqr(Z1Z1, a->Z); fe_sqr(Z2Z2, b->Z);
    mont_mul(U1, a->X, Z2Z2); mont_mul(U2, b->X, Z1Z1);
    mont_mul(t, a->Y, b->Z); mont_mul(S1, t, Z2Z2);            // Y1*Z2*Z2Z2
    mont_mul(t, b->Y, a->Z); mont_mul(S2, t, Z1Z1);            // Y2*Z1*Z1Z1
    fe_sub(H, U2, U1);
    fe_sub(rr, S2, S1);
    if (fe_is_zero(H)) {
        if (fe_is_zero(rr)) { jac_double(out, a); return; }    // a == b
        jac_set_id(out); return;                               // a == -b
    }
    fe I, J, r, V, X3, Y3, Z3, s1j;
    fe_add(t, H, H); fe_sqr(I, t); mont_mul(J, H, I);          // I=(2H)^2, J=H*I
    fe_add(r, rr, rr);                                         // 2*(S2-S1)
    mont_mul(V, U1, I);
    fe_sqr(X3, r); fe_sub(X3, X3, J); fe_add(t, V, V); fe_sub(X3, X3, t);   // r^2 - J - 2V
    fe_sub(t, V, X3); mont_mul(Y3, r, t);
    mont_mul(s1j, S1, J); fe_add(s1j, s1j, s1j); fe_sub(Y3, Y3, s1j);       // - 2*S1*J
    fe_add(t, a->Z, b->Z); fe_sqr(t, t); fe_sub(t, t, Z1Z1); fe_sub(t, t, Z2Z2);
    mont_mul(Z3, t, H);
    fe_copy(out->X, X3); fe_copy(out->Y, Y3); fe_copy(out->Z, Z3);
}

// out = k * P (double-and-add, MSB first). k is 32-byte big-endian.
static void jac_scalar(jac* out, const uint8_t k[32], const jac* P) {
    jac R; jac_set_id(&R);
    for (int i = 0; i < 256; i++) {
        jac_double(&R, &R);
        if ((k[i >> 3] >> (7 - (i & 7))) & 1) jac_add(&R, &R, P);
    }
    fe_copy(out->X, R.X); fe_copy(out->Y, R.Y); fe_copy(out->Z, R.Z);
}

// ---- byte I/O ------------------------------------------------------------------------
static void be_to_fe(fe r, const uint8_t b[32]) {
    for (int i = 0; i < 4; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | b[(3 - i) * 8 + j];   // limb 3 = most significant
        r[i] = v;
    }
}
static void fe_to_be(uint8_t b[32], const fe a) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) b[(3 - i) * 8 + j] = (uint8_t)(a[i] >> (8 * (7 - j)));
}

// Convert a Jacobian point to affine (x, y) big-endian, converting out of Montgomery form.
static int jac_to_affine(uint8_t x[32], uint8_t y[32], const jac* P) {
    if (jac_is_id(P)) return -1;
    fe zi, zi2, zi3, xa, ya;
    fe_inv(zi, P->Z);
    fe_sqr(zi2, zi); mont_mul(zi3, zi2, zi);
    mont_mul(xa, P->X, zi2); mont_mul(ya, P->Y, zi3);
    fe_from_mont(xa, xa); fe_from_mont(ya, ya);
    fe_to_be(x, xa); fe_to_be(y, ya);
    return 0;
}

// The base point G, in affine big-endian.
static const uint8_t GX[32] = {
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96 };
static const uint8_t GY[32] = {
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5 };

static void load_affine(jac* P, const uint8_t px[32], const uint8_t py[32]) {
    be_to_fe(P->X, px); be_to_fe(P->Y, py);
    fe_to_mont(P->X, P->X); fe_to_mont(P->Y, P->Y);
    fe_one_mont(P->Z);                              // Z = 1 (Montgomery)
}

int p256_base_scalar(const uint8_t k[32], uint8_t x[32], uint8_t y[32]) {
    jac G, R; load_affine(&G, GX, GY);
    jac_scalar(&R, k, &G);
    return jac_to_affine(x, y, &R);
}

int p256_point_scalar(const uint8_t k[32], const uint8_t px[32], const uint8_t py[32],
                      uint8_t x[32], uint8_t y[32]) {
    jac P, R; load_affine(&P, px, py);
    jac_scalar(&R, k, &P);
    return jac_to_affine(x, y, &R);
}

// ---- known-answer self-test ----------------------------------------------------------
static int hv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}
static void unhex32(uint8_t out[32], const char* h) {
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)((hv(h[2*i]) << 4) | hv(h[2*i+1]));
}
static int eq32(const uint8_t* a, const uint8_t* b) { for (int i=0;i<32;i++) if (a[i]!=b[i]) return 0; return 1; }

static int check_kg(const char* name, const char* kh, const char* xh, const char* yh) {
    uint8_t k[32], wx[32], wy[32], gx[32], gy[32];
    unhex32(k, kh); unhex32(wx, xh); unhex32(wy, yh);
    p256_base_scalar(k, gx, gy);
    if (eq32(gx, wx) && eq32(gy, wy)) { printf("p256: %s PASS\n", name); return 1; }
    printf("p256: %s FAIL\n", name);
    return 0;
}

int p256_selftest(void) {
    int pass = 0, total = 0;
    total++; pass += check_kg("2*G",
        "0000000000000000000000000000000000000000000000000000000000000002",
        "7cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978",
        "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1");
    total++; pass += check_kg("3*G",
        "0000000000000000000000000000000000000000000000000000000000000003",
        "5ecbe4d1a6330a44c8f7ef951d4bf165e6c6b721efada985fb41661bc6e7fd6c",
        "8734640c4998ff7e374b06ce1a64a2ecd82ab036384fb83d9a79b127a27d5032");
    total++; pass += check_kg("k*G (256-bit scalar)",
        "7d1e5c3a9f00b241668899aabbccddeeff0011223344556677889900aabbccdd",
        "c0992047b0ac48a44e2cc31fd92d4e9985204d1400ca7e88c9f97172ae635c26",
        "6841657fdaeaa4c2c843870953d5dd2882cd9e11145977072e4a17fea44c779d");
    printf("p256: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
