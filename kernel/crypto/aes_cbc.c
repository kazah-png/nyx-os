// ============================================================
// aes_cbc.c - AES-128-CBC (NIST SP 800-38A) + the FIPS-197 inverse cipher
// ============================================================
// The forward AES block (SubBytes/ShiftRows/MixColumns, column-major state, 11 round
// keys) lives in aes_gcm.c and is shared via aes_gcm.h. CBC decryption is the first
// caller that needs to run AES *backwards*, so the inverse cipher is added here:
// InvShiftRows, InvSubBytes (inverse S-box), InvMixColumns (GF(2^8) mix by 9/11/13/14),
// with the same round keys applied in reverse order. Correctness is pinned by the NIST
// SP 800-38A F.2 CBC-AES128 known-answer vectors (see aes_cbc_selftest).
#include "aes_cbc.h"
#include "aes_gcm.h"          // aes128_ctx, aes128_key_expand, aes128_encrypt

// AES inverse S-box (FIPS-197 Fig. 14): the exact inverse of the forward sbox in aes_gcm.c.
static const uint8_t inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b)); }

// Multiply x by n in GF(2^8), for the InvMixColumns coefficients n in {9,11,13,14}.
static uint8_t gmul(uint8_t x, uint8_t n) {
    uint8_t x2 = xtime(x), x4 = xtime(x2), x8 = xtime(x4), r = 0;
    if (n & 8) r ^= x8;
    if (n & 4) r ^= x4;
    if (n & 2) r ^= x2;
    if (n & 1) r ^= x;
    return r;
}

// The FIPS-197 §5.3 inverse cipher: decrypt one 16-byte block using the forward key
// schedule (rk[176]) applied from the last round key to the first. Public (declared in
// aes_gcm.h beside aes128_encrypt) so other block modes — e.g. AES Key Wrap — can reuse it.
void aes128_decrypt(const aes128_ctx* c, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16], t;
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ c->rk[10 * 16 + i];   // AddRoundKey(10)

    for (int round = 9; round >= 1; round--) {
        // InvShiftRows (state is column-major, s[col*4 + row]): rows 1/2/3 rotate right by 1/2/3.
        t = s[13]; s[13]=s[9];  s[9]=s[5];  s[5]=s[1];  s[1]=t;
        t = s[2];  s[2]=s[10];  s[10]=t;    t=s[6]; s[6]=s[14]; s[14]=t;
        t = s[3];  s[3]=s[7];   s[7]=s[11]; s[11]=s[15]; s[15]=t;

        for (int i = 0; i < 16; i++) s[i] = inv_sbox[s[i]];           // InvSubBytes

        const uint8_t* rk = c->rk + round * 16;                       // AddRoundKey(round)
        for (int i = 0; i < 16; i++) s[i] ^= rk[i];

        for (int col = 0; col < 4; col++) {                           // InvMixColumns
            uint8_t* p = s + col * 4;
            uint8_t a0=p[0], a1=p[1], a2=p[2], a3=p[3];
            p[0] = gmul(a0,14) ^ gmul(a1,11) ^ gmul(a2,13) ^ gmul(a3,9);
            p[1] = gmul(a0,9)  ^ gmul(a1,14) ^ gmul(a2,11) ^ gmul(a3,13);
            p[2] = gmul(a0,13) ^ gmul(a1,9)  ^ gmul(a2,14) ^ gmul(a3,11);
            p[3] = gmul(a0,11) ^ gmul(a1,13) ^ gmul(a2,9)  ^ gmul(a3,14);
        }
    }
    // Final round: no InvMixColumns.
    t = s[13]; s[13]=s[9];  s[9]=s[5];  s[5]=s[1];  s[1]=t;
    t = s[2];  s[2]=s[10];  s[10]=t;    t=s[6]; s[6]=s[14]; s[14]=t;
    t = s[3];  s[3]=s[7];   s[7]=s[11]; s[11]=s[15]; s[15]=t;
    for (int i = 0; i < 16; i++) s[i] = inv_sbox[s[i]];
    for (int i = 0; i < 16; i++) out[i] = s[i] ^ c->rk[i];            // AddRoundKey(0)
}

int aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                       const uint8_t* in, uint32_t len, uint8_t* out) {
    if (len % 16) return -1;
    aes128_ctx c; aes128_key_expand(&c, key);
    uint8_t prev[16];
    for (int i = 0; i < 16; i++) prev[i] = iv[i];
    for (uint32_t off = 0; off < len; off += 16) {
        uint8_t blk[16];
        for (int i = 0; i < 16; i++) blk[i] = in[off + i] ^ prev[i];  // XOR the previous ciphertext
        aes128_encrypt(&c, blk, out + off);
        for (int i = 0; i < 16; i++) prev[i] = out[off + i];
    }
    return 0;
}

int aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                       const uint8_t* in, uint32_t len, uint8_t* out) {
    if (len % 16) return -1;
    aes128_ctx c; aes128_key_expand(&c, key);
    uint8_t prev[16];
    for (int i = 0; i < 16; i++) prev[i] = iv[i];
    for (uint32_t off = 0; off < len; off += 16) {
        uint8_t cblk[16], dec[16];
        for (int i = 0; i < 16; i++) cblk[i] = in[off + i];           // save (so in == out is safe)
        aes128_decrypt(&c, cblk, dec);
        for (int i = 0; i < 16; i++) out[off + i] = dec[i] ^ prev[i];
        for (int i = 0; i < 16; i++) prev[i] = cblk[i];
    }
    return 0;
}

// ---- known-answer self-test (`aescbc`) ----
static int eq(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int aes_cbc_selftest(void) {
    // NIST SP 800-38A appendix F.2 CBC-AES128, key/IV shared by F.2.1 (encrypt) and F.2.2 (decrypt).
    static const uint8_t key[16] = {
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
    static const uint8_t iv[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
    static const uint8_t pt[64] = {
        0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
        0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51,
        0x30,0xc8,0x1c,0x46,0xa3,0x5c,0xe4,0x11,0xe5,0xfb,0xc1,0x19,0x1a,0x0a,0x52,0xef,
        0xf6,0x9f,0x24,0x45,0xdf,0x4f,0x9b,0x17,0xad,0x2b,0x41,0x7b,0xe6,0x6c,0x37,0x10 };
    static const uint8_t ct[64] = {
        0x76,0x49,0xab,0xac,0x81,0x19,0xb2,0x46,0xce,0xe9,0x8e,0x9b,0x12,0xe9,0x19,0x7d,
        0x50,0x86,0xcb,0x9b,0x50,0x72,0x19,0xee,0x95,0xdb,0x11,0x3a,0x91,0x76,0x78,0xb2,
        0x73,0xbe,0xd6,0xb8,0xe3,0xc1,0x74,0x3b,0x71,0x16,0xe6,0x9e,0x22,0x22,0x95,0x16,
        0x3f,0xf1,0xca,0xa1,0x68,0x1f,0xac,0x09,0x12,0x0e,0xca,0x30,0x75,0x86,0xe1,0xa7 };

    uint8_t buf[64], back[64]; int pass = 0, total = 0;

    total++; aes128_cbc_encrypt(key, iv, pt, 64, buf);
    if (eq(buf, ct, 64)) { pass++; printf("aescbc: NIST F.2.1 encrypt PASS\n"); }
    else printf("aescbc: encrypt FAIL\n");

    total++; aes128_cbc_decrypt(key, iv, ct, 64, back);
    if (eq(back, pt, 64)) { pass++; printf("aescbc: NIST F.2.2 decrypt PASS\n"); }
    else printf("aescbc: decrypt FAIL\n");

    // round-trip on a fresh key/IV, 3 blocks
    total++; {
        uint8_t k2[16], iv2[16], p2[48], c2[48], r2[48];
        for (int i = 0; i < 16; i++) { k2[i] = (uint8_t)(i * 3 + 1); iv2[i] = (uint8_t)(255 - i); }
        for (int i = 0; i < 48; i++) p2[i] = (uint8_t)(i * 7 + 2);
        aes128_cbc_encrypt(k2, iv2, p2, 48, c2);
        aes128_cbc_decrypt(k2, iv2, c2, 48, r2);
        if (eq(r2, p2, 48)) { pass++; printf("aescbc: round-trip 3 blocks PASS\n"); }
        else printf("aescbc: round-trip FAIL\n");
    }

    // in-place (in == out) round-trip: encrypt then decrypt over the same buffer
    total++; {
        uint8_t b2[32], orig[32];
        for (int i = 0; i < 32; i++) orig[i] = b2[i] = (uint8_t)(i * 5 + 9);
        aes128_cbc_encrypt(key, iv, b2, 32, b2);
        aes128_cbc_decrypt(key, iv, b2, 32, b2);
        if (eq(b2, orig, 32)) { pass++; printf("aescbc: in-place round-trip PASS\n"); }
        else printf("aescbc: in-place FAIL\n");
    }

    // a length that is not a whole number of blocks must be rejected
    total++; {
        uint8_t x[20], y[20];
        for (int i = 0; i < 20; i++) x[i] = (uint8_t)i;
        if (aes128_cbc_encrypt(key, iv, x, 20, y) != 0 && aes128_cbc_decrypt(key, iv, x, 20, y) != 0)
            { pass++; printf("aescbc: rejects non-block length PASS\n"); }
        else printf("aescbc: bad-length not rejected FAIL\n");
    }

    printf("aescbc: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
