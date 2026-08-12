// ============================================================
// aes_kw.c - AES Key Wrap (RFC 3394) + KAT (see aes_kw.h).
// ============================================================
#include "aes_kw.h"
#include "aes_gcm.h"      // aes128_ctx, aes128_key_expand, aes128_encrypt, aes128_decrypt

// RFC 3394 §2.2.3.1 default integrity check value (IV / "A6" pattern).
static const uint8_t KW_IV[8] = { 0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6,0xA6 };

int aes128_kw_wrap(const uint8_t kek[16], const uint8_t* in, uint32_t nblk, uint8_t* out) {
    if (nblk < 2) return -1;
    aes128_ctx c; aes128_key_expand(&c, kek);

    uint8_t a[8];
    for (int k = 0; k < 8; k++) a[k] = KW_IV[k];
    for (uint32_t i = 0; i < nblk * 8; i++) out[8 + i] = in[i];   // R[1..n] = P[1..n]

    for (int j = 0; j < 6; j++) {
        for (uint32_t i = 1; i <= nblk; i++) {
            uint8_t blk[16], b[16];
            for (int k = 0; k < 8; k++) blk[k]     = a[k];
            for (int k = 0; k < 8; k++) blk[8 + k] = out[i * 8 + k];
            aes128_encrypt(&c, blk, b);                            // B = AES(A | R[i])
            uint64_t t = (uint64_t)nblk * j + i;
            for (int k = 0; k < 8; k++) a[k] = b[k];               // A = MSB64(B)...
            for (int k = 0; k < 8; k++) a[7 - k] ^= (uint8_t)(t >> (8 * k));  // ...XOR t
            for (int k = 0; k < 8; k++) out[i * 8 + k] = b[8 + k]; // R[i] = LSB64(B)
        }
    }
    for (int k = 0; k < 8; k++) out[k] = a[k];                     // C[0] = A
    return 0;
}

int aes128_kw_unwrap(const uint8_t kek[16], const uint8_t* in, uint32_t nblk, uint8_t* out) {
    if (nblk < 2) return -1;
    aes128_ctx c; aes128_key_expand(&c, kek);

    uint8_t a[8];
    for (int k = 0; k < 8; k++) a[k] = in[k];                      // A = C[0]
    for (uint32_t i = 0; i < nblk * 8; i++) out[i] = in[8 + i];    // R[1..n] = C[1..n]

    for (int j = 5; j >= 0; j--) {
        for (uint32_t i = nblk; i >= 1; i--) {
            uint64_t t = (uint64_t)nblk * j + i;
            uint8_t blk[16], b[16];
            for (int k = 0; k < 8; k++) blk[k] = a[k];
            for (int k = 0; k < 8; k++) blk[7 - k] ^= (uint8_t)(t >> (8 * k));  // (A XOR t)...
            for (int k = 0; k < 8; k++) blk[8 + k] = out[(i - 1) * 8 + k];      // ...| R[i]
            aes128_decrypt(&c, blk, b);
            for (int k = 0; k < 8; k++) a[k] = b[k];                            // A = MSB64(B)
            for (int k = 0; k < 8; k++) out[(i - 1) * 8 + k] = b[8 + k];        // R[i] = LSB64(B)
        }
    }
    int ok = 1;                                                   // integrity: A must be the IV
    for (int k = 0; k < 8; k++) if (a[k] != KW_IV[k]) ok = 0;
    return ok ? 0 : -1;
}

// ---- known-answer self-test (`aeskw`) ----
int aes_kw_selftest(void) {
    // RFC 3394 §4.1: wrap a 128-bit key with a 128-bit KEK.
    static const uint8_t kek[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
    static const uint8_t key[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    static const uint8_t want[24] = {
        0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47, 0xae,0xf3,0x4b,0xd8,0xfb,0x5a,0x7b,0x82,
        0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5 };

    uint8_t wrapped[24];
    aes128_kw_wrap(kek, key, 2, wrapped);
    for (int i = 0; i < 24; i++) if (wrapped[i] != want[i]) return 1;

    uint8_t back[16];
    if (aes128_kw_unwrap(kek, wrapped, 2, back) != 0) return 2;   // round-trip must verify
    for (int i = 0; i < 16; i++) if (back[i] != key[i]) return 3;

    // A single flipped bit anywhere must make unwrap fail the integrity check.
    uint8_t tampered[24];
    for (int i = 0; i < 24; i++) tampered[i] = wrapped[i];
    tampered[10] ^= 0x01;
    if (aes128_kw_unwrap(kek, tampered, 2, back) == 0) return 4;

    // A longer key (256-bit, n=4) must round-trip too.
    static const uint8_t k32[32] = {
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
        0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f };
    uint8_t w40[40], b32[32];
    aes128_kw_wrap(kek, k32, 4, w40);
    if (aes128_kw_unwrap(kek, w40, 4, b32) != 0) return 5;
    for (int i = 0; i < 32; i++) if (b32[i] != k32[i]) return 6;

    return 0;
}
