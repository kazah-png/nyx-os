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
#include "sha256.h"

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

// Recover the padded message EM = sig^e mod N (big-endian, Nlen bytes). 0 on success, -1 on bad input.
static int rsa_recover_em(const uint8_t* Nb, uint32_t Nlen, uint64_t e,
                          const uint8_t* sig, uint32_t siglen, uint8_t* em) {
    if (Nlen == 0 || Nlen > RSA_MAX_LIMBS * 8 || siglen != Nlen) return -1;
    int L = (int)((Nlen + 7) / 8);
    bn N, S, M;
    bn_from_be(N, Nb, Nlen);
    bn_from_be(S, sig, siglen);
    if (bn_ge(S, N, L)) return -1;                   // signature must be < modulus
    bn_modexp(M, S, e, N, L);
    bn_to_be(M, em, Nlen);
    return 0;
}

// Check an EMSA-PKCS1-v1.5 EM: 0x00 0x01 0xFF..(>=8).. 0x00 || DigestInfo || hash. 0 if it matches.
static int rsa_pkcs1_check(const uint8_t* em, uint32_t Nlen, const uint8_t* DI, uint32_t dilen,
                           const uint8_t* hash, uint32_t hashlen) {
    if (em[0] != 0x00 || em[1] != 0x01) return -1;
    uint32_t i = 2;
    while (i < Nlen && em[i] == 0xFF) i++;
    if (i < 10 || i >= Nlen || em[i] != 0x00) return -1;
    i++;
    if (Nlen - i != dilen + hashlen) return -1;
    for (uint32_t j = 0; j < dilen; j++)   if (em[i + j]         != DI[j])   return -1;
    for (uint32_t j = 0; j < hashlen; j++) if (em[i + dilen + j] != hash[j]) return -1;
    return 0;
}

int rsa_pkcs1_sha256_verify(const uint8_t* Nb, uint32_t Nlen, uint64_t e,
                            const uint8_t* sig, uint32_t siglen, const uint8_t hash[32]) {
    uint8_t em[RSA_MAX_LIMBS * 8];
    if (rsa_recover_em(Nb, Nlen, e, sig, siglen, em)) return -1;
    static const uint8_t DI[19] = { 0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,     // DigestInfo(SHA-256)
                                    0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20 };
    return rsa_pkcs1_check(em, Nlen, DI, 19, hash, 32);
}

// RSA-PKCS1-v1.5 with SHA-512 (rsa_pkcs1_sha512, TLS 0x0601 — the last SKE scheme). Same padding, a
// different DigestInfo prefix (SHA-512 OID) and a 64-byte hash.
int rsa_pkcs1_sha512_verify(const uint8_t* Nb, uint32_t Nlen, uint64_t e,
                            const uint8_t* sig, uint32_t siglen, const uint8_t hash[64]) {
    uint8_t em[RSA_MAX_LIMBS * 8];
    if (rsa_recover_em(Nb, Nlen, e, sig, siglen, em)) return -1;
    static const uint8_t DI[19] = { 0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,     // DigestInfo(SHA-512)
                                    0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40 };
    return rsa_pkcs1_check(em, Nlen, DI, 19, hash, 64);
}

// SHA-256 of (a || b), either part optional.
static void sha256_cat(const uint8_t* a, uint32_t al, const uint8_t* b, uint32_t bl, uint8_t out[32]) {
    sha256_ctx_t c; sha256_init(&c);
    if (a && al) sha256_update(&c, a, al);
    if (b && bl) sha256_update(&c, b, bl);
    sha256_final(&c, out);
}

// RSASSA-PSS verify with SHA-256, MGF1-SHA256, salt length 32 (rsa_pss_rsae_sha256, TLS 0x0804).
// Returns 0 if the signature over `mHash` is valid under (N, e), -1 otherwise.
int rsa_pss_sha256_verify(const uint8_t* Nb, uint32_t Nlen, uint64_t e,
                          const uint8_t* sig, uint32_t siglen, const uint8_t mHash[32]) {
    if (Nlen == 0 || Nlen > RSA_MAX_LIMBS * 8 || siglen != Nlen) return -1;
    int L = (int)((Nlen + 7) / 8);
    bn N, S, M;
    bn_from_be(N, Nb, Nlen);
    bn_from_be(S, sig, siglen);
    if (bn_ge(S, N, L)) return -1;                   // signature must be < modulus
    bn_modexp(M, S, e, N, L);
    uint8_t em[RSA_MAX_LIMBS * 8];
    bn_to_be(M, em, Nlen);

    const uint32_t hLen = 32, sLen = 32;
    uint32_t modBits = 0;                            // bit length of N
    for (uint32_t i = 0; i < Nlen; i++) {
        if (Nb[i]) { int hb = 7; while (hb > 0 && !((Nb[i] >> hb) & 1)) hb--; modBits = (Nlen - 1 - i) * 8 + hb + 1; break; }
    }
    if (modBits == 0) return -1;
    uint32_t emBits = modBits - 1;
    uint32_t emLen  = (emBits + 7) / 8;
    if (emLen < hLen + sLen + 2 || emLen > Nlen) return -1;
    const uint8_t* EM = em + (Nlen - emLen);         // the emLen-byte encoded message
    if (EM[emLen - 1] != 0xbc) return -1;

    uint32_t dbLen = emLen - hLen - 1;               // EM = maskedDB(dbLen) || H(hLen) || 0xbc
    const uint8_t* maskedDB = EM;
    const uint8_t* H = EM + dbLen;
    uint32_t numMaskBits = 8 * emLen - emBits;       // top bits of maskedDB[0] that must be zero
    if (numMaskBits > 0 && (maskedDB[0] >> (8 - numMaskBits)) != 0) return -1;

    // DB = maskedDB XOR MGF1(H, dbLen)
    uint8_t DB[RSA_MAX_LIMBS * 8];
    uint32_t gen = 0, counter = 0;
    while (gen < dbLen) {
        uint8_t cb[4] = { (uint8_t)(counter >> 24), (uint8_t)(counter >> 16), (uint8_t)(counter >> 8), (uint8_t)counter };
        uint8_t md[32];
        sha256_cat(H, hLen, cb, 4, md);
        for (uint32_t j = 0; j < 32 && gen < dbLen; j++, gen++) DB[gen] = maskedDB[gen] ^ md[j];
        counter++;
    }
    if (numMaskBits > 0) DB[0] &= (uint8_t)(0xFF >> numMaskBits);

    // DB must be: PS(0x00...) || 0x01 || salt(sLen)
    uint32_t psLen = dbLen - sLen - 1;
    for (uint32_t i = 0; i < psLen; i++) if (DB[i] != 0x00) return -1;
    if (DB[psLen] != 0x01) return -1;
    const uint8_t* salt = DB + psLen + 1;

    // H' = SHA-256( 0x00*8 || mHash || salt );  valid iff H' == H
    uint8_t mp[8 + 32 + 32], hp[32];
    for (int i = 0; i < 8; i++) mp[i] = 0;
    for (int i = 0; i < 32; i++) mp[8 + i] = mHash[i];
    for (uint32_t i = 0; i < sLen; i++) mp[40 + i] = salt[i];
    sha256_cat(mp, 8 + 32 + sLen, 0, 0, hp);
    for (int i = 0; i < 32; i++) if (hp[i] != H[i]) return -1;
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

    // RSA-PKCS1-v1.5 with SHA-512 (TLS 0x0601): valid accepted, tampered hash rejected. Vector: cryptography lib.
    {
        static const char N512[] =
        "9996e71e9a7e578fc270ac4ac1d5913e68e98ee18bbeba38f442a86f9b981404"
        "90b20bb406d47f4bdd9744a952776753d0868f131dd2f7e5df0343dd73760d6e"
        "03de6629565ee67706485fd4b9887ef6902d6ea5baaf1b1d9e303d9e23640c80"
        "45b42e086084d784802e54f859e486c1c85971a7a57732212cd8c9f6378237ba"
        "655b6e28d6de2ef86d84ee3bb47fbba897a1a98264648c473128ea7cf13955a3"
        "280a90dba27c66185dc997222146f20b784c35d591e40773980edabdc565fdae"
        "8e5f81a2463b563b7ee16645361543f7964e828fc66f523c015e3fe2f980b174"
        "86740616a639b34cd21a8898708985e167206ff856f901a3ca5cebdbf3ed9253";
        static const char S512[] =
        "11b3c18c984f1d9d4f69a1fa14dab1e59bd684a01f5e92ff27797d1632331e62"
        "86b8c805488896eef1bdf7ac48de623644de68611d1770046465c965c915c1eb"
        "8ac231f5b36539076f2fbb3bc0e6c55587f50631e7f5d466262bf12c6b8dc5c6"
        "0d13dc2ead924d334f5fda264067d003c205d26d5f53eb04c1247cfffc56a4e0"
        "13f9d6650ae3b12a4a22afd6af1f4d9f374da31b4f1a5c81c482108fa38dadd0"
        "8ddadfaae81d419351cb6207fd1eb5e6615ae4a306700c69cca1e47dad94b0db"
        "bc867060448af855477cff808d17cf832e0afcbec71f25f3fd4dfa781eaae16e"
        "770a015d146837998b198a547d3c992c60c05ac979264ca0e58372ed185a3d56";
        static const char H512[] =
        "c5eb9e02c65825bfed59273b663600bb74aaf3812fd53c01c46f75297e1e743d"
        "67f21c71da20af968dc3999cda6e31af5340d1f3c0747baad5cd0cd0da513ba2";
        uint8_t Nb[256], sb[256], hb[64], hb2[64];
        unhexs(Nb, N512, 256); unhexs(sb, S512, 256); unhexs(hb, H512, 64);
        total++;
        if (rsa_pkcs1_sha512_verify(Nb, 256, 65537, sb, 256, hb) == 0) {
            pass++; printf("rsa: RSA-2048 PKCS1-v1.5 SHA-512 valid signature PASS\n");
        } else   printf("rsa: RSA-2048 PKCS1-v1.5 SHA-512 valid signature FAIL\n");
        for (int i = 0; i < 64; i++) hb2[i] = hb[i]; hb2[0] ^= 0x01;
        total++;
        if (rsa_pkcs1_sha512_verify(Nb, 256, 65537, sb, 256, hb2) == -1) {
            pass++; printf("rsa: SHA-512 tampered hash rejected PASS\n");
        } else   printf("rsa: SHA-512 tampered hash rejected FAIL\n");
    }

    // RSA-PSS (rsa_pss_rsae_sha256, TLS 0x0804): a valid PSS signature accepted, a tamper rejected.
    {
        static const char PSS_N[] =
            "b800c5db05821ef1e06bae15c30440078b82e5cf6f6de0f9d30b0672c672cb1b"
            "ab14de82bcaf55684faaba42462515915c531b3cee70cfdf217f01aeec40db66"
            "9150a2d19911b8ffd4cd55690f34b94e04fd4209ce15a5fc9c65e2e23202f67d"
            "d13d87e42ef0d9ee30de7eb92f82399a7b5925a14b4d92e7d8b3282fcd2d7986"
            "048bcb62f372895e01a51d723834e38e0c5919d89913aa66f37aa8fcd35e0a13"
            "6b45c6417ce090a13b189f51937e07a81d1d1c03d75cfc7240c7a15a0b7d82d0"
            "f1a4c946534e172a4dd096f1bcaebe358f6bd4841eed838e56695de17425aff4"
            "7275159870ace264e4aa2ee3ce2b525edc1b5336e9d23037a06f57f026cb9013";
        static const char PSS_SIG[] =
            "5a9e9430a170487ebe0186f486ba11e67d1d6bd5189169a9b3026c4d57cf668d"
            "616db1d1ea23d2ec0fd509074247590a9afc65ebc83635fa15884b2aeacf3ddc"
            "70d5577394ad841cd9ae41533227246f2f1dde6e705f99c726c77f1bedb255d4"
            "15bdc935295bb3212bc47fe96b3db1db8f3e6dc085e0c03f6dcaa4280901c6fa"
            "ee9b0849ccf59e622f38ea1bbc47bac3401a024104a6d7235ba5d3eaa4626478"
            "21507b8b7ef90263d227982804dfdb66f1f575f1dda6ef75514640780538fcfc"
            "e41e2649b6d9f911f897c8811e33316fa80311b68ff1a3a65fa3c42e98548280"
            "633e9aab6bba51f59389e16080ee327d36817fa25a2d748e393336b5798c64c4";
        static const char PSS_HASH[] = "e10133de6fe14070327bddd938bb525181ec7c323ff1a7e503e3a97dc3601794";
        uint8_t pn[256], psig[256], ph[32], ph2[32];
        unhexs(pn, PSS_N, 256); unhexs(psig, PSS_SIG, 256); unhexs(ph, PSS_HASH, 32);
        total++;
        if (rsa_pss_sha256_verify(pn, 256, 65537, psig, 256, ph) == 0) { pass++; printf("rsa: RSA-2048 PSS SHA-256 valid signature PASS\n"); }
        else   printf("rsa: RSA-2048 PSS SHA-256 valid signature FAIL\n");
        for (int i = 0; i < 32; i++) ph2[i] = ph[i]; ph2[5] ^= 0x01;
        total++;
        if (rsa_pss_sha256_verify(pn, 256, 65537, psig, 256, ph2) == -1) { pass++; printf("rsa: PSS tampered hash rejected PASS\n"); }
        else   printf("rsa: PSS tampered hash rejected FAIL\n");
    }

    printf("rsa: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
