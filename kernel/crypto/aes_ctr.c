// ============================================================
// aes_ctr.c - AES-128 in CTR mode (NIST SP 800-38A)
// ============================================================
// The confidentiality-only counter mode: to encrypt (or decrypt — CTR is symmetric),
// AES-encrypt a running 128-bit counter block into a keystream and XOR it with the data,
// advancing the counter by one (big-endian, with carry) after each 16-byte block. Unlike
// GCM this carries no authentication and unlike CMAC it is not a MAC; it is the raw stream
// primitive behind CTR-DRBG and disk/record encryption. The block cipher is the vetted
// FIPS-197 core exposed from aes_gcm.c (no second S-box/key schedule). Correctness is
// pinned by the four NIST SP 800-38A §F.5.1 CTR-AES128 vectors via aes_ctr_selftest()
// (the CI `aesctr` KAT), plus a decrypt round-trip and a partial-final-block check.
#include "../core/kernel.h"
#include "aes_gcm.h"
#include "aes_ctr.h"

// counter += 1, treating the 16 bytes as one big-endian integer (wraps at 2^128).
static void ctr_inc(uint8_t c[16]) {
    for (int i = 15; i >= 0; i--) {
        if (++c[i] != 0) break;   // no carry out of this byte -> done
    }
}

void aes128_ctr_xcrypt(const uint8_t key[16], const uint8_t counter[16],
                       const uint8_t* in, uint8_t* out, uint32_t len) {
    aes128_ctx c;
    aes128_key_expand(&c, key);
    uint8_t ctr[16], ks[16];
    for (int i = 0; i < 16; i++) ctr[i] = counter[i];

    uint32_t off = 0;
    while (off < len) {
        aes128_encrypt(&c, ctr, ks);              // keystream block = E_K(counter)
        uint32_t n = len - off;
        if (n > 16) n = 16;                        // final block may be partial
        for (uint32_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
        ctr_inc(ctr);
        off += n;
    }
}

// ---- Known-answer test: NIST SP 800-38A §F.5 -----------------------------------------
static int eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
int aes_ctr_selftest(void) {
    static const uint8_t key[16] = {
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
    static const uint8_t ctr0[16] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff };
    static const uint8_t pt[64] = {
        0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a,
        0xae,0x2d,0x8a,0x57,0x1e,0x03,0xac,0x9c,0x9e,0xb7,0x6f,0xac,0x45,0xaf,0x8e,0x51,
        0x30,0xc8,0x1c,0x46,0xa3,0x5c,0xe4,0x11,0xe5,0xfb,0xc1,0x19,0x1a,0x0a,0x52,0xef,
        0xf6,0x9f,0x24,0x45,0xdf,0x4f,0x9b,0x17,0xad,0x2b,0x41,0x7b,0xe6,0x6c,0x37,0x10 };
    static const uint8_t ct[64] = {   // SP 800-38A F.5.1 CTR-AES128.Encrypt ciphertext
        0x87,0x4d,0x61,0x91,0xb6,0x20,0xe3,0x26,0x1b,0xef,0x68,0x64,0x99,0x0d,0xb6,0xce,
        0x98,0x06,0xf6,0x6b,0x79,0x70,0xfd,0xff,0x86,0x17,0x18,0x7b,0xb9,0xff,0xfd,0xff,
        0x5a,0xe4,0xdf,0x3e,0xdb,0xd5,0xd3,0x5e,0x5b,0x4f,0x09,0x02,0x0d,0xb0,0x3e,0xab,
        0x1e,0x03,0x1d,0xda,0x2f,0xbe,0x03,0xd1,0x79,0x21,0x70,0xa0,0xf3,0x00,0x9c,0xee };

    uint8_t out[64], back[64];
    // Encrypt the full 4 blocks (exercises the counter carry across FEFF->FF00->FF01->FF02).
    aes128_ctr_xcrypt(key, ctr0, pt, out, 64);
    if (!eq(out, ct, 64)) return 1;
    // Decrypt is the same operation.
    aes128_ctr_xcrypt(key, ctr0, ct, back, 64);
    if (!eq(back, pt, 64)) return 2;
    // Partial length (20 bytes): must equal the first 20 ciphertext bytes.
    for (int i = 0; i < 64; i++) out[i] = 0;
    aes128_ctr_xcrypt(key, ctr0, pt, out, 20);
    if (!eq(out, ct, 20)) return 3;
    // A single-byte length still uses the first keystream byte.
    aes128_ctr_xcrypt(key, ctr0, pt, out, 1);
    if (out[0] != ct[0]) return 4;
    return 0;
}
