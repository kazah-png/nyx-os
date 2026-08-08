// ============================================================
// SipHash-2-4 — a keyed PRF / hash-flooding-resistant hash (RFC-less but a de
// facto standard; Aumasson & Bernstein).
// ============================================================
// Four 64-bit words are seeded from the key and two ASCII constants, each 8-byte
// little-endian message block is absorbed with two SipRounds, a final block
// carries the length in its top byte, and four more rounds finalise. No tables,
// no data-dependent branches — constant-time by construction.

#include "siphash.h"

static inline uint64_t rotl64(uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }

static inline uint64_t load64le(const uint8_t* p) {
    return (uint64_t)p[0]        | ((uint64_t)p[1] << 8)  | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

#define SIPROUND                                              \
    do {                                                      \
        v0 += v1; v1 = rotl64(v1, 13); v1 ^= v0; v0 = rotl64(v0, 32); \
        v2 += v3; v3 = rotl64(v3, 16); v3 ^= v2;              \
        v0 += v3; v3 = rotl64(v3, 21); v3 ^= v0;              \
        v2 += v1; v1 = rotl64(v1, 17); v1 ^= v2; v2 = rotl64(v2, 32); \
    } while (0)

uint64_t siphash24(const uint8_t key[16], const uint8_t* data, uint32_t len) {
    uint64_t k0 = load64le(key), k1 = load64le(key + 8);
    uint64_t v0 = k0 ^ 0x736f6d6570736575ULL;   // "somepseu"
    uint64_t v1 = k1 ^ 0x646f72616e646f6dULL;   // "dorandom"
    uint64_t v2 = k0 ^ 0x6c7967656e657261ULL;   // "lygenera"
    uint64_t v3 = k1 ^ 0x7465646279746573ULL;   // "tedbytes"

    const uint8_t* end = data + (len & ~(uint32_t)7);
    for (; data != end; data += 8) {
        uint64_t m = load64le(data);
        v3 ^= m; SIPROUND; SIPROUND; v0 ^= m;
    }

    uint64_t b = (uint64_t)len << 56;           // final block: length in the top byte
    int left = (int)(len & 7);
    for (int i = 0; i < left; i++) b |= (uint64_t)data[i] << (8 * i);
    v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;

    v2 ^= 0xff; SIPROUND; SIPROUND; SIPROUND; SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

// ---------------- Known-answer test ----------------
int siphash_selftest(void) {
    uint8_t key[16];
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)i;      // key = 00 01 … 0f
    uint8_t msg[64];
    for (int i = 0; i < 64; i++) msg[i] = (uint8_t)i;      // msg[i] = i

    // Reference SipHash-2-4 outputs for msg[0..len-1] under the above key.
    static const struct { uint32_t len; uint64_t mac; } V[] = {
        {  0, 0x726fdb47dd0e0e31ULL },
        {  1, 0x74f839c593dc67fdULL },
        {  7, 0xab0200f58b01d137ULL },
        {  8, 0x93f5f5799a932462ULL },
        { 15, 0xa129ca6149be45e5ULL },
        { 16, 0x3f2acc7f57c29bdbULL },
        { 32, 0x7127512f72f27cceULL },
        { 63, 0x958a324ceb064572ULL },
    };
    for (int i = 0; i < (int)(sizeof(V) / sizeof(V[0])); i++)
        if (siphash24(key, msg, V[i].len) != V[i].mac) return i + 1;

    // Sensitivity: a key-bit and a message-bit must each change the MAC.
    uint8_t k2[16]; for (int i = 0; i < 16; i++) k2[i] = key[i]; k2[0] ^= 1;
    if (siphash24(key, msg, 16) == siphash24(k2, msg, 16)) return 100;
    uint8_t m2[16]; for (int i = 0; i < 16; i++) m2[i] = msg[i]; m2[3] ^= 0x80;
    if (siphash24(key, msg, 16) == siphash24(key, m2, 16)) return 101;
    return 0;
}
