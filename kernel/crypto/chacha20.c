// ============================================================
// ChaCha20 stream cipher (RFC 8439).
// ============================================================
// The state is 16 little-endian 32-bit words: 4 constants ("expand 32-byte k"),
// 8 key words, a 32-bit block counter, and 3 nonce words. Twenty rounds of the
// quarter-round (10 column + 10 diagonal) then add-back the original state gives
// a 64-byte keystream block; encryption XORs that stream into the data.

#include "chacha20.h"

static inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static inline uint32_t load32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void store32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#define QR(a, b, c, d)                       \
    a += b; d ^= a; d = rotl32(d, 16);       \
    c += d; b ^= c; b = rotl32(b, 12);       \
    a += b; d ^= a; d = rotl32(d, 8);        \
    c += d; b ^= c; b = rotl32(b, 7)

void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t s[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i]  = load32le(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; i++) s[13 + i] = load32le(nonce + 4 * i);

    uint32_t x[16];
    for (int i = 0; i < 16; i++) x[i] = s[i];
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8],  x[12]); QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]); QR(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) store32le(out + 4 * i, x[i] + s[i]);
}

void chacha20_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                  const uint8_t* in, uint8_t* out, uint32_t len) {
    uint8_t ks[64];
    uint32_t off = 0;
    while (off < len) {
        chacha20_block(key, counter + off / 64, nonce, ks);
        uint32_t n = len - off; if (n > 64) n = 64;
        for (uint32_t i = 0; i < n; i++) out[off + i] = in[off + i] ^ ks[i];
        off += n;
    }
}

// ---------------- Known-answer test (RFC 8439) ----------------
static int cc_eq(const uint8_t* a, const uint8_t* b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int chacha20_selftest(void) {
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;

    // RFC 8439 §2.3.2 — block function, counter=1
    static const uint8_t KS[64] = {
        0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f,
        0xa3, 0x20, 0x71, 0xc4, 0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03,
        0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e, 0xd2, 0x82, 0x64, 0x46,
        0x07, 0x9f, 0xaa, 0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
        0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8,
        0xa2, 0x50, 0x3c, 0x4e,
    };
    {
        uint8_t nonce[12] = { 0,0,0,9, 0,0,0,0x4a, 0,0,0,0 };
        uint8_t out[64];
        chacha20_block(key, 1, nonce, out);
        if (!cc_eq(out, KS, 64)) return 1;
    }

    // RFC 8439 §2.4.2 — encryption, counter=1
    static const uint8_t CT[114] = {
        0x6e, 0x2e, 0x35, 0x9a, 0x25, 0x68, 0xf9, 0x80, 0x41, 0xba, 0x07, 0x28,
        0xdd, 0x0d, 0x69, 0x81, 0xe9, 0x7e, 0x7a, 0xec, 0x1d, 0x43, 0x60, 0xc2,
        0x0a, 0x27, 0xaf, 0xcc, 0xfd, 0x9f, 0xae, 0x0b, 0xf9, 0x1b, 0x65, 0xc5,
        0x52, 0x47, 0x33, 0xab, 0x8f, 0x59, 0x3d, 0xab, 0xcd, 0x62, 0xb3, 0x57,
        0x16, 0x39, 0xd6, 0x24, 0xe6, 0x51, 0x52, 0xab, 0x8f, 0x53, 0x0c, 0x35,
        0x9f, 0x08, 0x61, 0xd8, 0x07, 0xca, 0x0d, 0xbf, 0x50, 0x0d, 0x6a, 0x61,
        0x56, 0xa3, 0x8e, 0x08, 0x8a, 0x22, 0xb6, 0x5e, 0x52, 0xbc, 0x51, 0x4d,
        0x16, 0xcc, 0xf8, 0x06, 0x81, 0x8c, 0xe9, 0x1a, 0xb7, 0x79, 0x37, 0x36,
        0x5a, 0xf9, 0x0b, 0xbf, 0x74, 0xa3, 0x5b, 0xe6, 0xb4, 0x0b, 0x8e, 0xed,
        0xf2, 0x78, 0x5e, 0x42, 0x87, 0x4d,
    };
    {
        const char* pt = "Ladies and Gentlemen of the class of '99: If I could offer you "
                         "only one tip for the future, sunscreen would be it.";
        uint8_t nonce[12] = { 0,0,0,0, 0,0,0,0x4a, 0,0,0,0 };
        uint8_t ct[114], back[114];
        chacha20_xor(key, 1, nonce, (const uint8_t*)pt, ct, 114);
        if (!cc_eq(ct, CT, 114)) return 2;
        chacha20_xor(key, 1, nonce, ct, back, 114);           // decrypt is the same op
        if (!cc_eq(back, (const uint8_t*)pt, 114)) return 3;
    }

    // Sensitivity: one key bit must change the whole keystream block.
    {
        uint8_t k2[32]; for (int i = 0; i < 32; i++) k2[i] = key[i];
        k2[0] ^= 1;
        uint8_t nonce[12] = { 0 }, a[64], b[64];
        chacha20_block(key, 0, nonce, a);
        chacha20_block(k2,  0, nonce, b);
        if (cc_eq(a, b, 64)) return 4;
    }
    return 0;
}
