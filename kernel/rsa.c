// ============================================================
// rsa.c - RSA PKCS#1 v1.5 signature verification, v5.9.62
// ============================================================
// The last crypto primitive for the TLS trust model: real certificate chains almost always
// have an RSA root or intermediate CA, so verifying a chain up to a trusted anchor needs
// RSA-PKCS1-v1.5 signature verification. Verify is cheap (the public exponent is tiny, e =
// 65537), so this uses a straightforward variable-length big-integer modexp — schoolbook
// double-and-add modular multiply, no Montgomery constants — followed by the PKCS#1 v1.5
// (SHA-256) unpadding. Pinned by `rsatest` against an RSA-2048 signature from a standard
// library. The certificate-chain walking that uses this is the next increment.
#include "kernel.h"
#include "rsa.h"

#define RSA_MAX_LIMBS 64                 // up to RSA-4096
typedef uint64_t bn[RSA_MAX_LIMBS];      // little-endian 64-bit limbs

static void bn_from_be(bn r, const uint8_t* b, uint32_t len) {
    for (int i = 0; i < RSA_MAX_LIMBS; i++) r[i] = 0;
    for (uint32_t k = 0; k < len; k++) r[k >> 3] |= (uint64_t)b[len - 1 - k] << (8 * (k & 7));
}
static void bn_to_be(const bn a, uint8_t* out, uint32_t len) {
    for (uint32_t k = 0; k < len; k++) out[len - 1 - k] = (uint8_t)(a[k >> 3] >> (8 * (k & 7)));
}
static int bn_ge(const bn a, const bn b, int L) {
    for (int i = L - 1; i >= 0; i--) { if (a[i] > b[i]) return 1; if (a[i] < b[i]) return 0; }
    return 1;
}
static void bn_sub(bn r, const bn a, const bn b, int L) {
    unsigned __int128 brw = 0;
    for (int i = 0; i < L; i++) { unsigned __int128 d = (unsigned __int128)a[i] - b[i] - brw; r[i] = (uint64_t)d; brw = (uint64_t)((d >> 64) & 1); }
}

// r = 2r mod N. r < N on entry; 2r < 2N, so one conditional subtraction (the carry out of the
// shift is absorbed by the subtraction's borrow when it fires).
static void bn_dbl_mod(bn r, const bn N, int L) {
    uint64_t carry = 0;
    for (int i = 0; i < L; i++) { uint64_t nc = r[i] >> 63; r[i] = (r[i] << 1) | carry; carry = nc; }
    if (carry || bn_ge(r, N, L)) { bn t; bn_sub(t, r, N, L); for (int i = 0; i < L; i++) r[i] = t[i]; }
}
// r = (r + a) mod N. r, a < N.
static void bn_add_mod(bn r, const bn a, const bn N, int L) {
    unsigned __int128 c = 0;
    for (int i = 0; i < L; i++) { c += (unsigned __int128)r[i] + a[i]; r[i] = (uint64_t)c; c >>= 64; }
    if ((uint64_t)c || bn_ge(r, N, L)) { bn t; bn_sub(t, r, N, L); for (int i = 0; i < L; i++) r[i] = t[i]; }
}
// r = a*b mod N (double-and-add over the bits of b, MSB first).
static void bn_mulmod(bn r, const bn a, const bn b, const bn N, int L) {
    bn acc; for (int i = 0; i < L; i++) acc[i] = 0;
    for (int i = L * 64 - 1; i >= 0; i--) {
        bn_dbl_mod(acc, N, L);
        if ((b[i >> 6] >> (i & 63)) & 1) bn_add_mod(acc, a, N, L);
    }
    for (int i = 0; i < L; i++) r[i] = acc[i];
}
// r = base^e mod N (e small).
static void bn_modexp(bn r, const bn base, uint64_t e, const bn N, int L) {
    bn result; for (int i = 0; i < L; i++) result[i] = 0; result[0] = 1;
    int top = 63; while (top > 0 && !((e >> top) & 1)) top--;
    for (int i = top; i >= 0; i--) {
        bn t;
        bn_mulmod(t, result, result, N, L); for (int k = 0; k < L; k++) result[k] = t[k];   // square
        if ((e >> i) & 1) { bn_mulmod(t, result, base, N, L); for (int k = 0; k < L; k++) result[k] = t[k]; }
    }
    for (int i = 0; i < L; i++) r[i] = result[i];
}

int rsa_pkcs1_sha256_verify(const uint8_t* Nb, uint32_t Nlen, uint64_t e,
                            const uint8_t* sig, uint32_t siglen, const uint8_t hash[32]) {
    if (Nlen == 0 || Nlen > RSA_MAX_LIMBS * 8 || siglen != Nlen) return -1;
    int L = (int)((Nlen + 7) / 8);
    bn N, S, M;
    bn_from_be(N, Nb, Nlen);
    bn_from_be(S, sig, siglen);
    if (bn_ge(S, N, L)) return -1;                   // signature must be < modulus
    bn_modexp(M, S, e, N, L);

    uint8_t em[RSA_MAX_LIMBS * 8];
    bn_to_be(M, em, Nlen);
    // EMSA-PKCS1-v1.5: 0x00 0x01 0xFF..(>=8).. 0x00 || DigestInfo(SHA-256) || hash
    if (em[0] != 0x00 || em[1] != 0x01) return -1;
    uint32_t i = 2;
    while (i < Nlen && em[i] == 0xFF) i++;
    if (i < 10 || i >= Nlen || em[i] != 0x00) return -1;
    i++;
    static const uint8_t DI[19] = { 0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,
                                    0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20 };
    if (Nlen - i != 19 + 32) return -1;
    for (int j = 0; j < 19; j++) if (em[i + j]      != DI[j])   return -1;
    for (int j = 0; j < 32; j++) if (em[i + 19 + j] != hash[j]) return -1;
    return 0;
}

// ---- known-answer self-test ----------------------------------------------------------
static int hv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void unhexs(uint8_t* out, const char* h, int n) {
    for (int i = 0; i < n; i++) out[i] = (uint8_t)((hv(h[2 * i]) << 4) | hv(h[2 * i + 1]));
}

static const char RSA_N_HEX[] =
    "b2ec2be0526b83ae56f6054381c235527a3950679b40bcba91357ca695f6f114"
    "9f0f5dfcd47acd37744f3f97848164a57e6dace42697775d2d62b4c95aa2632f"
    "247bc87c5f677591a13d5841b2082bf03c01ec33f1f892d2eadf558d94434f34"
    "a01bdf0b47e4c9170d1af76942ae7b596d4bf8bc234ed75381fc9e70cd8bc639"
    "606ca021d4ceac7e5e7928fe0251e422f3ff00d08b691be2487ef05aee912135"
    "e551650c666a5616964fbc2cfea60ab8f975ccb1d956e8a8f0365b739813b73a"
    "1bb28d8fcd0a7204ca80c7cc97a053ebbe3da0285f9346a069ba01b07f299efb"
    "f96bd9d20a0482c53a38c59b1f08f9e1ffa88a2efdff68a870eeb3363a162da7";
static const char RSA_SIG_HEX[] =
    "4e44f84a6f6a35a4307265f75f4be9e5fa95faeef4d9d31ec247867e6c9cf7f0"
    "4ee9f8aff99e8b1bfe221e49c80cf8c02a8dcdad20bfafffad1c3f63ea5b0da4"
    "4379d6faf74630949d15fe1d0b595930cf832e9b1733c52eaf7a72441ac92e6b"
    "482e67344f74837d5e2f91e312cd4e3446bb7bf31f850ad87f4204277a692388"
    "c4ae17fae7b51b8c4d888c223bab3c50e6da92a85846a04cfbd48387f299417c"
    "33298121fe8412eb451ed659e74dd38251f1b4ed3ba88adead39eb65c669e95e"
    "712b197febcb1ba5700eee2b5f4e31c51e1024f21336166c9623144a53793b61"
    "57cb49fcae4e6a879979e951975b697e3dadd0d2064ef94f24a96cdfe49642bf";

int rsa_selftest(void) {
    uint8_t N[256], sig[256], hash[32], h2[32], s2[256];
    unhexs(N, RSA_N_HEX, 256);
    unhexs(sig, RSA_SIG_HEX, 256);
    unhexs(hash, "e82eed60e60578cb904702db59560e8781c45a361c09bc4e8e16cbcc1753a9f8", 32);
    int pass = 0, total = 0;

    total++;
    if (rsa_pkcs1_sha256_verify(N, 256, 65537, sig, 256, hash) == 0) {
        pass++; printf("rsa: RSA-2048 PKCS1-v1.5 SHA-256 valid signature PASS\n");
    } else   printf("rsa: RSA-2048 PKCS1-v1.5 SHA-256 valid signature FAIL\n");

    for (int i = 0; i < 32; i++) h2[i] = hash[i]; h2[0] ^= 0x01;
    total++;
    if (rsa_pkcs1_sha256_verify(N, 256, 65537, sig, 256, h2) == -1) {
        pass++; printf("rsa: tampered hash rejected PASS\n");
    } else   printf("rsa: tampered hash rejected FAIL\n");

    for (int i = 0; i < 256; i++) s2[i] = sig[i]; s2[100] ^= 0x01;
    total++;
    if (rsa_pkcs1_sha256_verify(N, 256, 65537, s2, 256, hash) == -1) {
        pass++; printf("rsa: tampered signature rejected PASS\n");
    } else   printf("rsa: tampered signature rejected FAIL\n");

    printf("rsa: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
