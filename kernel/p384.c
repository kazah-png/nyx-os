// ============================================================
// p384.c - NIST P-384 (secp384r1) field + point arithmetic, v5.9.64
// ============================================================
// The curve the certificate *chain* signs with: inspecting example.com's real chain showed the
// CA links use ECDSA-P384 / SHA-384, so verifying those links needs P-384 point arithmetic under
// each issuer's public key. Same shape as p256.c but one size up: GF(p) arithmetic (Montgomery
// multiplication, 6x64-bit limbs, native __int128 products) and point add / double / scalar-
// multiply in Jacobian coordinates (a = -3), plus affine I/O and ECDSA verify (mod-n scalar side
// + the verification equation). Pinned by `p384test` against NIST base-point multiples and an
// ECDSA-P384/SHA-384 known-answer signature. With this + SHA-384, the chain walk (v5.9.65) has
// every primitive it needs.
#include "kernel.h"
#include "p384.h"

typedef uint64_t fe[6];                 // field element: 6 little-endian 64-bit limbs

// P-384 prime p and R^2 mod p (for Montgomery conversion). n0' = -p^-1 mod 2^64.
static const uint64_t Pp[6] = { 0x00000000ffffffffULL, 0xffffffff00000000ULL,
                                0xfffffffffffffffeULL, 0xffffffffffffffffULL,
                                0xffffffffffffffffULL, 0xffffffffffffffffULL };
static const fe FE_R2       = { 0xfffffffe00000001ULL, 0x0000000200000000ULL,
                                0xfffffffe00000000ULL, 0x0000000200000000ULL,
                                0x0000000000000001ULL, 0x0000000000000000ULL };
static const uint64_t P0    = 0x0000000100000001ULL;                 // -p^-1 mod 2^64

static void fe_copy(fe r, const fe a) { for (int i = 0; i < 6; i++) r[i] = a[i]; }
static int  fe_is_zero(const fe a)    { return (a[0]|a[1]|a[2]|a[3]|a[4]|a[5]) == 0; }
static int  fe_eq(const fe a, const fe b) {
    return ((a[0]^b[0])|(a[1]^b[1])|(a[2]^b[2])|(a[3]^b[3])|(a[4]^b[4])|(a[5]^b[5])) == 0;
}

static void fe_add(fe r, const fe a, const fe b) {
    uint64_t t[6]; unsigned __int128 c = 0;
    for (int i = 0; i < 6; i++) { c += (unsigned __int128)a[i] + b[i]; t[i] = (uint64_t)c; c >>= 64; }
    uint64_t carry = (uint64_t)c;
    uint64_t s[6]; unsigned __int128 brw = 0;
    for (int i = 0; i < 6; i++) { unsigned __int128 d = (unsigned __int128)t[i] - Pp[i] - brw; s[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (carry | (brw ^ 1));            // subtract p iff sum >= p
    for (int i = 0; i < 6; i++) r[i] = (s[i] & mask) | (t[i] & ~mask);
}

static void fe_sub(fe r, const fe a, const fe b) {
    uint64_t t[6]; unsigned __int128 brw = 0;
    for (int i = 0; i < 6; i++) { unsigned __int128 d = (unsigned __int128)a[i] - b[i] - brw; t[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (uint64_t)brw;                  // add p back on borrow
    unsigned __int128 c = 0;
    for (int i = 0; i < 6; i++) { c += (unsigned __int128)t[i] + (Pp[i] & mask); r[i] = (uint64_t)c; c >>= 64; }
}

// Montgomery multiply mod `mod`: r = a*b*R^-1 mod `mod` (CIOS). n0 = -mod^-1 mod 2^64.
// Used for both the field (mod p) and the scalar ring (mod n, for ECDSA).
static void mont_core(fe r, const fe a, const fe b, const uint64_t mod[6], uint64_t n0) {
    uint64_t t[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) {
        unsigned __int128 C = 0;
        for (int j = 0; j < 6; j++) {
            unsigned __int128 s = (unsigned __int128)t[j] + (unsigned __int128)a[j] * b[i] + C;
            t[j] = (uint64_t)s; C = s >> 64;
        }
        unsigned __int128 s = (unsigned __int128)t[6] + C; t[6] = (uint64_t)s; t[7] = (uint64_t)(s >> 64);
        uint64_t m = t[0] * n0;
        C = (unsigned __int128)t[0] + (unsigned __int128)m * mod[0]; C >>= 64;   // low limb cancels
        for (int j = 1; j < 6; j++) {
            unsigned __int128 s2 = (unsigned __int128)t[j] + (unsigned __int128)m * mod[j] + C;
            t[j-1] = (uint64_t)s2; C = s2 >> 64;
        }
        unsigned __int128 s3 = (unsigned __int128)t[6] + C; t[5] = (uint64_t)s3; C = s3 >> 64;
        t[6] = t[7] + (uint64_t)C;
    }
    uint64_t s[6]; unsigned __int128 brw = 0;
    for (int i = 0; i < 6; i++) { unsigned __int128 d = (unsigned __int128)t[i] - mod[i] - brw; s[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (t[6] | (brw ^ 1));
    for (int i = 0; i < 6; i++) r[i] = (s[i] & mask) | (t[i] & ~mask);
}
static void mont_mul(fe r, const fe a, const fe b) { mont_core(r, a, b, Pp, P0); }

static void fe_sqr(fe r, const fe a)      { mont_mul(r, a, a); }
static void fe_to_mont(fe r, const fe a)  { mont_mul(r, a, FE_R2); }         // a -> a*R
static void fe_from_mont(fe r, const fe a){ fe o = {1,0,0,0,0,0}; mont_mul(r, a, o); }   // a*R -> a
static void fe_one_mont(fe r)             { fe o = {1,0,0,0,0,0}; fe_to_mont(r, o); }

// r = a^-1 mod p  (Fermat: a^(p-2)), all in Montgomery form.
static void fe_inv(fe r, const fe a) {
    static const uint64_t E[6] = { 0x00000000fffffffdULL, 0xffffffff00000000ULL,
                                   0xfffffffffffffffeULL, 0xffffffffffffffffULL,
                                   0xffffffffffffffffULL, 0xffffffffffffffffULL };   // p - 2
    fe result; fe_one_mont(result);
    for (int i = 383; i >= 0; i--) {
        fe_sqr(result, result);
        if ((E[i >> 6] >> (i & 63)) & 1) mont_mul(result, result, a);
    }
    fe_copy(r, result);
}

// ---- scalar arithmetic mod n (the P-384 group order), for ECDSA -------------------------
static const uint64_t Nn[6]   = { 0xecec196accc52973ULL, 0x581a0db248b0a77aULL,
                                  0xc7634d81f4372ddfULL, 0xffffffffffffffffULL,
                                  0xffffffffffffffffULL, 0xffffffffffffffffULL };
static const uint64_t N0      = 0x6ed46089e88fdc45ULL;                 // -n^-1 mod 2^64
static const fe        FE_R2N = { 0x2d319b2419b409a9ULL, 0xff3d81e5df1aa419ULL,
                                  0xbc3e483afcb82947ULL, 0xd40d49174aab1cc5ULL,
                                  0x3fb05b7a28266895ULL, 0x0c84ee012b39bf21ULL };   // R^2 mod n

static void montn_mul(fe r, const fe a, const fe b) { mont_core(r, a, b, Nn, N0); }
static void fen_to_mont(fe r, const fe a)   { montn_mul(r, a, FE_R2N); }
static void fen_from_mont(fe r, const fe a) { fe o = {1,0,0,0,0,0}; montn_mul(r, a, o); }
static void fen_one_mont(fe r)              { fe o = {1,0,0,0,0,0}; fen_to_mont(r, o); }

// r = a^-1 mod n  (Fermat: a^(n-2)), Montgomery form.
static void fen_inv(fe r, const fe a) {
    static const uint64_t E[6] = { 0xecec196accc52971ULL, 0x581a0db248b0a77aULL,
                                   0xc7634d81f4372ddfULL, 0xffffffffffffffffULL,
                                   0xffffffffffffffffULL, 0xffffffffffffffffULL };   // n - 2
    fe result; fen_one_mont(result);
    for (int i = 383; i >= 0; i--) {
        montn_mul(result, result, result);
        if ((E[i >> 6] >> (i & 63)) & 1) montn_mul(result, result, a);
    }
    fe_copy(r, result);
}

// Reduce a value < 2n mod n (one conditional subtraction).
static void reduce_n(fe r) {
    uint64_t t[6]; unsigned __int128 brw = 0;
    for (int i = 0; i < 6; i++) { unsigned __int128 d = (unsigned __int128)r[i] - Nn[i] - brw; t[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
    uint64_t mask = 0 - (brw ^ 1);            // take r-n iff r >= n (no borrow)
    for (int i = 0; i < 6; i++) r[i] = (t[i] & mask) | (r[i] & ~mask);
}
static int fe_lt_n(const fe a) {              // 1 if a < n
    unsigned __int128 brw = 0;
    for (int i = 0; i < 6; i++) { unsigned __int128 d = (unsigned __int128)a[i] - Nn[i] - brw; brw = (uint64_t)((d >> 64) & 1); }
    return (int)brw;
}

// ---- points in Jacobian coordinates (X:Y:Z), field elements in Montgomery form ----------
typedef struct { fe X, Y, Z; } jac;

static void jac_set_id(jac* p) { fe_one_mont(p->X); fe_one_mont(p->Y); for (int i=0;i<6;i++) p->Z[i]=0; }
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

// out = k * P (double-and-add, MSB first). k is 48-byte big-endian.
static void jac_scalar(jac* out, const uint8_t k[48], const jac* P) {
    jac R; jac_set_id(&R);
    for (int i = 0; i < 384; i++) {
        jac_double(&R, &R);
        if ((k[i >> 3] >> (7 - (i & 7))) & 1) jac_add(&R, &R, P);
    }
    fe_copy(out->X, R.X); fe_copy(out->Y, R.Y); fe_copy(out->Z, R.Z);
}

// ---- byte I/O ------------------------------------------------------------------------
static void be_to_fe(fe r, const uint8_t b[48]) {
    for (int i = 0; i < 6; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) v = (v << 8) | b[(5 - i) * 8 + j];   // limb 5 = most significant
        r[i] = v;
    }
}
static void fe_to_be(uint8_t b[48], const fe a) {
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 8; j++) b[(5 - i) * 8 + j] = (uint8_t)(a[i] >> (8 * (7 - j)));
}

// Convert a Jacobian point to affine (x, y) big-endian, converting out of Montgomery form.
static int jac_to_affine(uint8_t x[48], uint8_t y[48], const jac* P) {
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
static const uint8_t GX[48] = {
    0xaa,0x87,0xca,0x22,0xbe,0x8b,0x05,0x37,0x8e,0xb1,0xc7,0x1e,0xf3,0x20,0xad,0x74,
    0x6e,0x1d,0x3b,0x62,0x8b,0xa7,0x9b,0x98,0x59,0xf7,0x41,0xe0,0x82,0x54,0x2a,0x38,
    0x55,0x02,0xf2,0x5d,0xbf,0x55,0x29,0x6c,0x3a,0x54,0x5e,0x38,0x72,0x76,0x0a,0xb7 };
static const uint8_t GY[48] = {
    0x36,0x17,0xde,0x4a,0x96,0x26,0x2c,0x6f,0x5d,0x9e,0x98,0xbf,0x92,0x92,0xdc,0x29,
    0xf8,0xf4,0x1d,0xbd,0x28,0x9a,0x14,0x7c,0xe9,0xda,0x31,0x13,0xb5,0xf0,0xb8,0xc0,
    0x0a,0x60,0xb1,0xce,0x1d,0x7e,0x81,0x9d,0x7a,0x43,0x1d,0x7c,0x90,0xea,0x0e,0x5f };

static void load_affine(jac* P, const uint8_t px[48], const uint8_t py[48]) {
    be_to_fe(P->X, px); be_to_fe(P->Y, py);
    fe_to_mont(P->X, P->X); fe_to_mont(P->Y, P->Y);
    fe_one_mont(P->Z);                              // Z = 1 (Montgomery)
}

int p384_base_scalar(const uint8_t k[48], uint8_t x[48], uint8_t y[48]) {
    jac G, R; load_affine(&G, GX, GY);
    jac_scalar(&R, k, &G);
    return jac_to_affine(x, y, &R);
}

int p384_point_scalar(const uint8_t k[48], const uint8_t px[48], const uint8_t py[48],
                      uint8_t x[48], uint8_t y[48]) {
    jac P, R; load_affine(&P, px, py);
    jac_scalar(&R, k, &P);
    return jac_to_affine(x, y, &R);
}

// ECDSA-P384 verify (FIPS 186): return 0 if signature (r,s) over `hash` is valid under the
// public key Q = (Qx, Qy), -1 otherwise. All inputs are 48-byte big-endian.
int p384_ecdsa_verify(const uint8_t Qx[48], const uint8_t Qy[48], const uint8_t hash[48],
                      const uint8_t r_be[48], const uint8_t s_be[48]) {
    fe r, s, e;
    be_to_fe(r, r_be); be_to_fe(s, s_be); be_to_fe(e, hash);
    if (fe_is_zero(r) || fe_is_zero(s) || !fe_lt_n(r) || !fe_lt_n(s)) return -1;  // r,s in [1,n-1]
    reduce_n(e);                                        // e mod n

    fe sm, wm, em, rm, u1m, u2m, u1, u2;
    fen_to_mont(sm, s); fen_inv(wm, sm);                // w = s^-1 (Montgomery)
    fen_to_mont(em, e); montn_mul(u1m, em, wm); fen_from_mont(u1, u1m);   // u1 = e*w mod n
    fen_to_mont(rm, r); montn_mul(u2m, rm, wm); fen_from_mont(u2, u2m);   // u2 = r*w mod n

    uint8_t u1b[48], u2b[48];
    fe_to_be(u1b, u1); fe_to_be(u2b, u2);

    jac G, Q, R1, R2, RR;                               // R = u1*G + u2*Q
    load_affine(&G, GX, GY);
    load_affine(&Q, Qx, Qy);
    jac_scalar(&R1, u1b, &G);
    jac_scalar(&R2, u2b, &Q);
    jac_add(&RR, &R1, &R2);
    if (jac_is_id(&RR)) return -1;

    uint8_t rx[48], ry[48];
    jac_to_affine(rx, ry, &RR);
    fe v; be_to_fe(v, rx); reduce_n(v);                 // v = R.x mod n
    return fe_eq(v, r) ? 0 : -1;                        // valid iff v == r
}

// ---- known-answer self-test ----------------------------------------------------------
static int hv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}
static void unhex48(uint8_t out[48], const char* h) {
    for (int i = 0; i < 48; i++) out[i] = (uint8_t)((hv(h[2*i]) << 4) | hv(h[2*i+1]));
}
static int eq48(const uint8_t* a, const uint8_t* b) { for (int i=0;i<48;i++) if (a[i]!=b[i]) return 0; return 1; }

static int check_kg(const char* name, const char* kh, const char* xh, const char* yh) {
    uint8_t k[48], wx[48], wy[48], gx[48], gy[48];
    unhex48(k, kh); unhex48(wx, xh); unhex48(wy, yh);
    p384_base_scalar(k, gx, gy);
    if (eq48(gx, wx) && eq48(gy, wy)) { printf("p384: %s PASS\n", name); return 1; }
    printf("p384: %s FAIL\n", name);
    return 0;
}

int p384_selftest(void) {
    int pass = 0, total = 0;
    total++; pass += check_kg("2*G",
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000002",
        "08d999057ba3d2d969260045c55b97f089025959a6f434d651d207d19fb96e9e4fe0e86ebe0e64f85b96a9c75295df61",
        "8e80f1fa5b1b3cedb7bfe8dffd6dba74b275d875bc6cc43e904e505f256ab4255ffd43e94d39e22d61501e700a940e80");
    total++; pass += check_kg("3*G",
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000003",
        "077a41d4606ffa1464793c7e5fdc7d98cb9d3910202dcd06bea4f240d3566da6b408bbae5026580d02d7e5c70500c831",
        "c995f7ca0b0c42837d0bbe9602a9fc998520b41c85115aa5f7684c0edc111eacc24abd6be4b5d298b65f28600a2f1df1");
    total++; pass += check_kg("k*G (384-bit scalar)",
        "00007d1e5c3a9f00b241668899aabbccddeeff0011223344556677889900aabbccddeeff00112233445566778899aabb",
        "684e9ba3692d5353de101fb52a96976adc9d97b0f5f2b29fe6c97e36f311af7743b81a0cdd6ef2d55d56085530c164ab",
        "9a706459b8dfbdc977586a886b4c0abcb541e023960aabb64b4c9713bd95b7306ba758da8b2eaf7852119139b3a8582d");

    // ECDSA-P384/SHA-384 verify: a valid signature must be accepted and a tampered one rejected.
    {
        static const uint8_t Qx[48] = {
            0x68,0xf1,0x9d,0x74,0x45,0xdd,0x63,0xfa,0x0b,0xaf,0x21,0x83,0x56,0x35,0xbb,0x85,
            0xc6,0x63,0x73,0xa8,0x8f,0x30,0x5e,0x38,0xd4,0x96,0xd2,0x72,0xfb,0x49,0x50,0xd9,
            0x81,0xb8,0xe9,0x83,0xb0,0x40,0x70,0x52,0x1b,0x1a,0xd5,0x93,0x06,0x38,0x46,0xe5 };
        static const uint8_t Qy[48] = {
            0x29,0x91,0x28,0xa3,0x17,0x2e,0x3d,0xfb,0x26,0xb0,0xa2,0x71,0xac,0x6f,0x80,0x6e,
            0x60,0x30,0x72,0x33,0x39,0xb8,0x67,0x3c,0x71,0x15,0x2a,0xfd,0x06,0x6d,0xae,0x11,
            0x1d,0x75,0x40,0x17,0x75,0xf4,0x5c,0x5e,0x91,0xae,0xf8,0xd3,0x0c,0x0d,0xab,0xe4 };
        static const uint8_t h[48] = {
            0x4b,0x12,0x61,0x2b,0x43,0xf0,0x02,0x09,0x4d,0x61,0xa3,0xfb,0xdd,0x16,0x6a,0xfe,
            0xa7,0x27,0x45,0x7f,0x38,0x7b,0xdc,0x75,0xd7,0x9b,0x37,0xea,0x88,0x34,0x73,0x64,
            0xa6,0xd2,0x9b,0x11,0xdd,0x74,0x3b,0xc1,0x50,0xe7,0xef,0xe2,0x2d,0x63,0x12,0xca };
        static const uint8_t rsig[48] = {
            0x4b,0xdb,0x89,0x0c,0x37,0xbc,0x17,0xae,0x9d,0x1d,0xb6,0x2c,0x23,0xc7,0x32,0xa0,
            0xb5,0x24,0x1e,0x19,0xa2,0x04,0x70,0x15,0x72,0x4a,0x10,0x3e,0x9b,0xb1,0x48,0x91,
            0x0c,0x7f,0x05,0xc2,0x47,0x6c,0x63,0x8a,0x54,0xac,0x7e,0xc6,0xfb,0x3b,0x56,0x2a };
        static const uint8_t ssig[48] = {
            0x7c,0x6a,0xe0,0x7e,0xd0,0x67,0xcb,0x06,0x06,0x4b,0xea,0x2c,0xd1,0x5f,0x47,0xe6,
            0xfc,0x3d,0xbb,0xf0,0x31,0xd0,0xf9,0x24,0x90,0xec,0x39,0x27,0x3b,0xd8,0xf8,0xce,
            0x0a,0xfc,0x26,0x50,0x12,0x17,0x54,0x5e,0x54,0x14,0xfd,0x18,0xb7,0xeb,0x98,0xd6 };
        uint8_t ht[48]; for (int i = 0; i < 48; i++) ht[i] = h[i];
        int good = (p384_ecdsa_verify(Qx, Qy, h, rsig, ssig) == 0);
        ht[0] ^= 0x01;                                  // tamper the hash
        int bad = (p384_ecdsa_verify(Qx, Qy, ht, rsig, ssig) == -1);
        total++;
        if (good && bad) { pass++; printf("p384: ECDSA-P384 verify PASS (valid accepted, tampered rejected)\n"); }
        else               printf("p384: ECDSA-P384 verify FAIL (good=%d bad=%d)\n", good, bad);
    }

    printf("p384: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
