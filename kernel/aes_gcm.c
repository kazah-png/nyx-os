// ============================================================
// aes_gcm.c - AES-128-GCM authenticated encryption, v5.9.52
// ============================================================
// The record-protection layer of the https arc: TLS negotiated
// TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 (0xc02b), so every application byte travels
// inside an AES-128-GCM AEAD record keyed by the material v5.9.51 derived. This module
// implements AES-128 (FIPS-197) and GCM (NIST SP 800-38D): CTR-mode encryption plus a
// GHASH over GF(2^128) for authentication. GCM needs only AES *encryption* (CTR is
// symmetric and the hash subkey is E_K(0)), so there is no decrypt/InvMixColumns path.
//
// It is standalone here — the handshake still stops after the key schedule; wiring GCM
// into ClientKeyExchange/Finished and the record layer is the next increment. Correctness
// is pinned by the FIPS-197 block vector and the McGrew-Viega GCM test cases (encrypt,
// decrypt, and a tamper-detection check) via the `gcmtest` command. Not constant-time
// (table S-box, bitwise GF multiply) — fine to prove the mechanism; a hardening pass is
// a later concern, noted alongside the CSPRNG work.
#include "kernel.h"
#include "aes_gcm.h"

// ---- AES-128 (FIPS-197), encryption only --------------------------------------------

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t Rcon[11] = { 0x8d,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };

typedef struct { uint8_t rk[176]; } aes128_ctx;   // 11 round keys

static void aes128_key_expand(aes128_ctx* c, const uint8_t key[16]) {
    uint8_t* rk = c->rk;
    for (int i = 0; i < 16; i++) rk[i] = key[i];
    uint8_t t[4];
    for (int i = 4; i < 44; i++) {
        int k = (i - 1) * 4;
        t[0] = rk[k+0]; t[1] = rk[k+1]; t[2] = rk[k+2]; t[3] = rk[k+3];
        if (i % 4 == 0) {
            uint8_t tmp = t[0]; t[0] = t[1]; t[1] = t[2]; t[2] = t[3]; t[3] = tmp;   // RotWord
            t[0] = sbox[t[0]]; t[1] = sbox[t[1]]; t[2] = sbox[t[2]]; t[3] = sbox[t[3]];
            t[0] ^= Rcon[i / 4];
        }
        int j = i * 4; k = (i - 4) * 4;
        rk[j+0] = rk[k+0] ^ t[0]; rk[j+1] = rk[k+1] ^ t[1];
        rk[j+2] = rk[k+2] ^ t[2]; rk[j+3] = rk[k+3] ^ t[3];
    }
}

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }

static void aes128_encrypt(const aes128_ctx* c, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ c->rk[i];    // AddRoundKey(0)
    for (int round = 1; round <= 10; round++) {
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];       // SubBytes

        uint8_t t;                                            // ShiftRows (state is column-major)
        t = s[1];  s[1]=s[5];  s[5]=s[9];  s[9]=s[13]; s[13]=t;
        t = s[2];  s[2]=s[10]; s[10]=t;    t=s[6]; s[6]=s[14]; s[14]=t;
        t = s[15]; s[15]=s[11];s[11]=s[7]; s[7]=s[3];  s[3]=t;

        if (round < 10) {                                     // MixColumns
            for (int col = 0; col < 4; col++) {
                uint8_t* p = s + col * 4;
                uint8_t a0=p[0], a1=p[1], a2=p[2], a3=p[3];
                uint8_t all = a0 ^ a1 ^ a2 ^ a3;
                p[0] ^= all ^ xtime(a0 ^ a1);
                p[1] ^= all ^ xtime(a1 ^ a2);
                p[2] ^= all ^ xtime(a2 ^ a3);
                p[3] ^= all ^ xtime(a3 ^ a0);
            }
        }
        const uint8_t* rk = c->rk + round * 16;               // AddRoundKey(round)
        for (int i = 0; i < 16; i++) s[i] ^= rk[i];
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
}

// ---- GCM: GHASH over GF(2^128) + CTR -------------------------------------------------

// Z = X * Y in GF(2^128) with the GCM reduction polynomial (R = 0xe1 || 0^120).
static void gf_mult(const uint8_t* X, const uint8_t* Y, uint8_t* Z) {
    uint8_t V[16];
    for (int i = 0; i < 16; i++) { V[i] = Y[i]; Z[i] = 0; }
    for (int i = 0; i < 128; i++) {
        if ((X[i >> 3] >> (7 - (i & 7))) & 1)
            for (int j = 0; j < 16; j++) Z[j] ^= V[j];
        uint8_t lsb = V[15] & 1;                              // V >>= 1 (128-bit, big-endian)
        for (int j = 15; j > 0; j--) V[j] = (uint8_t)((V[j] >> 1) | (V[j-1] << 7));
        V[0] >>= 1;
        if (lsb) V[0] ^= 0xe1;
    }
}

// GHASH the AAD then the ciphertext then the length block, into out.
static void ghash(const uint8_t H[16], const uint8_t* aad, uint32_t aad_len,
                  const uint8_t* ct, uint32_t ct_len, uint8_t out[16]) {
    uint8_t Y[16], blk[16], t[16];
    for (int i = 0; i < 16; i++) Y[i] = 0;

    for (uint32_t i = 0; i < aad_len; i += 16) {
        uint32_t n = aad_len - i; if (n > 16) n = 16;
        for (int j = 0; j < 16; j++) blk[j] = (j < (int)n) ? aad[i + j] : 0;
        for (int j = 0; j < 16; j++) Y[j] ^= blk[j];
        gf_mult(Y, H, t); for (int j = 0; j < 16; j++) Y[j] = t[j];
    }
    for (uint32_t i = 0; i < ct_len; i += 16) {
        uint32_t n = ct_len - i; if (n > 16) n = 16;
        for (int j = 0; j < 16; j++) blk[j] = (j < (int)n) ? ct[i + j] : 0;
        for (int j = 0; j < 16; j++) Y[j] ^= blk[j];
        gf_mult(Y, H, t); for (int j = 0; j < 16; j++) Y[j] = t[j];
    }
    uint64_t abits = (uint64_t)aad_len * 8, cbits = (uint64_t)ct_len * 8;   // len(A) || len(C), bits
    for (int j = 0; j < 8; j++) blk[j]     = (uint8_t)(abits >> (8 * (7 - j)));
    for (int j = 0; j < 8; j++) blk[8 + j] = (uint8_t)(cbits >> (8 * (7 - j)));
    for (int j = 0; j < 16; j++) Y[j] ^= blk[j];
    gf_mult(Y, H, t); for (int j = 0; j < 16; j++) out[j] = t[j];
}

static void inc32(uint8_t* ctr) {              // increment the rightmost 32 bits
    for (int i = 15; i >= 12; i--) { if (++ctr[i] != 0) break; }
}

// CTR-mode XOR of src->dst, starting from counter block ctr (advanced per block).
static void gctr(const aes128_ctx* c, uint8_t* ctr,
                 const uint8_t* src, uint32_t len, uint8_t* dst) {
    uint8_t ks[16];
    for (uint32_t i = 0; i < len; i += 16) {
        aes128_encrypt(c, ctr, ks);
        uint32_t n = len - i; if (n > 16) n = 16;
        for (uint32_t j = 0; j < n; j++) dst[i + j] = src[i + j] ^ ks[j];
        inc32(ctr);
    }
}

// Compute H, J0 (96-bit IV path) and the tag over aad+ct into tag_out.
static void gcm_tag(const aes128_ctx* c, const uint8_t H[16], const uint8_t iv[12],
                    const uint8_t* aad, uint32_t aad_len,
                    const uint8_t* ct, uint32_t ct_len, uint8_t tag_out[16]) {
    uint8_t J0[16], S[16], EJ0[16];
    for (int i = 0; i < 12; i++) J0[i] = iv[i];
    J0[12] = 0; J0[13] = 0; J0[14] = 0; J0[15] = 1;
    ghash(H, aad, aad_len, ct, ct_len, S);
    aes128_encrypt(c, J0, EJ0);
    for (int i = 0; i < 16; i++) tag_out[i] = S[i] ^ EJ0[i];   // T = GHASH XOR E_K(J0)
}

void aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* pt, uint32_t pt_len,
                        uint8_t* ct, uint8_t tag[16]) {
    aes128_ctx c; aes128_key_expand(&c, key);
    uint8_t H[16], zero[16];
    for (int i = 0; i < 16; i++) zero[i] = 0;
    aes128_encrypt(&c, zero, H);                 // hash subkey H = E_K(0)

    uint8_t ctr[16];                             // start CTR at J0 + 1
    for (int i = 0; i < 12; i++) ctr[i] = iv[i];
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 1;
    inc32(ctr);
    gctr(&c, ctr, pt, pt_len, ct);

    gcm_tag(&c, H, iv, aad, aad_len, ct, pt_len, tag);
}

int aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                       const uint8_t* aad, uint32_t aad_len,
                       const uint8_t* ct, uint32_t ct_len,
                       const uint8_t tag[16], uint8_t* pt) {
    aes128_ctx c; aes128_key_expand(&c, key);
    uint8_t H[16], zero[16];
    for (int i = 0; i < 16; i++) zero[i] = 0;
    aes128_encrypt(&c, zero, H);

    uint8_t want[16];
    gcm_tag(&c, H, iv, aad, aad_len, ct, ct_len, want);   // verify BEFORE decrypting
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= (uint8_t)(want[i] ^ tag[i]);
    if (diff) return -1;

    uint8_t ctr[16];
    for (int i = 0; i < 12; i++) ctr[i] = iv[i];
    ctr[12] = 0; ctr[13] = 0; ctr[14] = 0; ctr[15] = 1;
    inc32(ctr);
    gctr(&c, ctr, ct, ct_len, pt);
    return 0;
}

// ---- known-answer self-test ---------------------------------------------------------

static int hv(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 0;
}
static uint32_t unhex(uint8_t* out, const char* h) {         // returns byte count
    uint32_t n = 0;
    while (h[0] && h[1]) { out[n++] = (uint8_t)((hv(h[0]) << 4) | hv(h[1])); h += 2; }
    return n;
}
static int eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

// One GCM test case: encrypt must reproduce (want_ct, want_tag), and decrypt must recover
// pt and accept the tag. Returns 1 on full pass.
static int gcm_case(const char* name, const char* kh, const char* ivh, const char* aadh,
                    const char* pth, const char* cth, const char* tagh) {
    uint8_t key[16], iv[12], aad[64], pt[80], want_ct[80], want_tag[16], ct[80], tag[16], out[80];
    unhex(key, kh); unhex(iv, ivh);
    uint32_t aad_len = unhex(aad, aadh);
    uint32_t pt_len  = unhex(pt, pth);
    unhex(want_ct, cth); unhex(want_tag, tagh);

    aes128_gcm_encrypt(key, iv, aad, aad_len, pt, pt_len, ct, tag);
    int enc_ok = eq(ct, want_ct, pt_len) && eq(tag, want_tag, 16);

    int dec_ok = (aes128_gcm_decrypt(key, iv, aad, aad_len, ct, pt_len, tag, out) == 0)
                 && eq(out, pt, pt_len);

    if (enc_ok && dec_ok) { printf("gcm: %s encrypt+decrypt PASS\n", name); return 1; }
    printf("gcm: %s FAIL (enc=%d dec=%d)\n", name, enc_ok, dec_ok);
    return 0;
}

int aes_gcm_selftest(void) {
    int pass = 0, total = 0;

    // AES-128 single block, FIPS-197 Appendix C.1.
    {
        aes128_ctx c; uint8_t k[16], in[16], o[16], w[16];
        unhex(k, "000102030405060708090a0b0c0d0e0f");
        unhex(in, "00112233445566778899aabbccddeeff");
        unhex(w, "69c4e0d86a7b0430d8cdb78070b4c55a");
        aes128_key_expand(&c, k); aes128_encrypt(&c, in, o);
        total++;
        if (eq(o, w, 16)) { pass++; printf("gcm: AES-128 block (FIPS-197) PASS\n"); }
        else                printf("gcm: AES-128 block (FIPS-197) FAIL\n");
    }

    // McGrew-Viega GCM test cases (AES-128).
    total++; pass += gcm_case("TC2 (empty AAD, 1 block)",
        "00000000000000000000000000000000", "000000000000000000000000", "",
        "00000000000000000000000000000000",
        "0388dace60b6a392f328c2b971b2fe78", "ab6e47d42cec13bdf53a67b21257bddf");

    total++; pass += gcm_case("TC3 (4 blocks, no AAD)",
        "feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888", "",
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985",
        "4d5c2af327cd64a62cf35abd2ba6fab4");

    total++; pass += gcm_case("TC4 (partial blocks + AAD)",
        "feffe9928665731c6d6a8f9467308308", "cafebabefacedbaddecaf888",
        "feedfacedeadbeeffeedfacedeadbeefabaddad2",
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
        "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091",
        "5bc94fbc3221a5db94fae95ae7121a47");

    // Tamper detection: a flipped tag byte on TC2 must be REJECTED.
    {
        uint8_t key[16], iv[12], ct[16], tag[16], out[16];
        unhex(key, "00000000000000000000000000000000");
        unhex(iv, "000000000000000000000000");
        unhex(ct, "0388dace60b6a392f328c2b971b2fe78");
        unhex(tag, "ab6e47d42cec13bdf53a67b21257bddf");
        tag[0] ^= 0x01;                                  // corrupt one bit
        total++;
        if (aes128_gcm_decrypt(key, iv, 0, 0, ct, 16, tag, out) == -1) {
            pass++; printf("gcm: tamper detection PASS (bad tag rejected)\n");
        } else printf("gcm: tamper detection FAIL (bad tag accepted!)\n");
    }

    printf("gcm: self-test %d/%d cases passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
