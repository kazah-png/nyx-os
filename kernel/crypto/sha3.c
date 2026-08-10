// ============================================================
// sha3.c - SHA3-256 (FIPS 202), the Keccak sponge hash
// ============================================================
// The third hash *family* in the tree, and a fundamentally different shape from the other
// two: SHA-2 is Merkle-Damgard and BLAKE2s is an ARX chain, while SHA-3 is a SPONGE. A
// 1600-bit state (5x5 lanes of 64 bits) is permuted by Keccak-f[1600] (24 rounds of the
// theta/rho/pi/chi/iota steps); the message is absorbed rate bytes at a time (136 for
// SHA3-256, leaving a 512-bit capacity), padded with the SHA-3 domain suffix 0x06 and the
// pad10*1 terminator 0x80, then 32 bytes are squeezed out. x86_64 is little-endian, so the
// lane state doubles as the byte buffer with no swapping. Correctness is pinned by
// sha3_selftest() (the CI `sha3` KAT) against the published FIPS 202 vectors.
#include "../core/kernel.h"
#include "sha3.h"

#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))

static const uint64_t keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL,
};
static const int keccak_rotc[24] = { 1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                                     27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44 };
static const int keccak_piln[24] = { 10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
                                     15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1 };

static void keccakf(uint64_t st[25]) {
    for (int round = 0; round < 24; round++) {
        uint64_t bc[5], t;
        for (int i = 0; i < 5; i++)                        // Theta
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i + 4) % 5] ^ ROTL64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5) st[j + i] ^= t;
        }
        t = st[1];                                          // Rho + Pi
        for (int i = 0; i < 24; i++) {
            int j = keccak_piln[i];
            bc[0] = st[j];
            st[j] = ROTL64(t, keccak_rotc[i]);
            t = bc[0];
        }
        for (int j = 0; j < 25; j += 5) {                   // Chi
            for (int i = 0; i < 5; i++) bc[i] = st[j + i];
            for (int i = 0; i < 5; i++) st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
        st[0] ^= keccak_rc[round];                          // Iota
    }
}

void sha3_256(const uint8_t* in, uint32_t inlen, uint8_t out[32]) {
    uint64_t st[25];
    for (int i = 0; i < 25; i++) st[i] = 0;
    uint8_t* sb = (uint8_t*)st;                             // LE machine: lanes ARE the byte buffer
    const uint32_t rate = 136;                              // SHA3-256: 1088-bit rate, 512-bit capacity

    uint32_t i = 0;
    while (inlen - i >= rate) {                             // absorb whole rate blocks
        for (uint32_t k = 0; k < rate; k++) sb[k] ^= in[i + k];
        keccakf(st);
        i += rate;
    }
    uint32_t rem = inlen - i;                               // final partial block + pad10*1
    for (uint32_t k = 0; k < rem; k++) sb[k] ^= in[i + k];
    sb[rem]      ^= 0x06;                                    // SHA-3 domain separation + first pad bit
    sb[rate - 1] ^= 0x80;                                    // last pad bit
    keccakf(st);

    for (int k = 0; k < 32; k++) out[k] = sb[k];            // squeeze 256 bits (rate > 32, one block)
}

// ---- Known-answer test: FIPS 202 SHA3-256 -------------------------------------------
static int eq(const uint8_t* a, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
int sha3_selftest(void) {
    uint8_t d[32];
    static const uint8_t empty[32] = {
        0xa7,0xff,0xc6,0xf8,0xbf,0x1e,0xd7,0x66,0x51,0xc1,0x47,0x56,0xa0,0x61,0xd6,0x62,
        0xf5,0x80,0xff,0x4d,0xe4,0x3b,0x49,0xfa,0x82,0xd8,0x0a,0x4b,0x80,0xf8,0x43,0x4a };
    sha3_256((const uint8_t*)"", 0, d);
    if (!eq(d, empty, 32)) return 1;

    static const uint8_t abc[32] = {
        0x3a,0x98,0x5d,0xa7,0x4f,0xe2,0x25,0xb2,0x04,0x5c,0x17,0x2d,0x6b,0xd3,0x90,0xbd,
        0x85,0x5f,0x08,0x6e,0x3e,0x9d,0x52,0x5b,0x46,0xbf,0xe2,0x45,0x11,0x43,0x15,0x32 };
    sha3_256((const uint8_t*)"abc", 3, d);
    if (!eq(d, abc, 32)) return 2;

    static const uint8_t fox[32] = {
        0x69,0x07,0x0d,0xda,0x01,0x97,0x5c,0x8c,0x12,0x0c,0x3a,0xad,0xa1,0xb2,0x82,0x39,
        0x4e,0x7f,0x03,0x2f,0xa9,0xcf,0x32,0xf4,0xcb,0x22,0x59,0xa0,0x89,0x7d,0xfc,0x04 };
    sha3_256((const uint8_t*)"The quick brown fox jumps over the lazy dog", 43, d);
    if (!eq(d, fox, 32)) return 3;

    // A multi-block message (200 bytes, all 0xA3): exercises the absorb loop past one rate
    // block (136) and the following pad-only handling. Vector from FIPS 202 / Keccak KATs.
    uint8_t a3[200];
    for (int k = 0; k < 200; k++) a3[k] = 0xA3;
    static const uint8_t a3d[32] = {
        0x79,0xf3,0x8a,0xde,0xc5,0xc2,0x03,0x07,0xa9,0x8e,0xf7,0x6e,0x83,0x24,0xaf,0xbf,
        0xd4,0x6c,0xfd,0x81,0xb2,0x2e,0x39,0x73,0xc6,0x5f,0xa1,0xbd,0x9d,0xe3,0x17,0x87 };
    sha3_256(a3, 200, d);
    if (!eq(d, a3d, 32)) return 4;
    return 0;
}
