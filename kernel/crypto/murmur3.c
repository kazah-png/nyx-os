#include "murmur3.h"

static uint32_t murmur_rotl32(uint32_t x, int r) {
    return (x << r) | (x >> (32 - r));
}

uint32_t murmur3_32(const uint8_t* data, uint32_t len, uint32_t seed) {
    const uint32_t c1 = 0xcc9e2d51u, c2 = 0x1b873593u;
    uint32_t h = seed;
    uint32_t i = 0;

    // body: consume 4-byte little-endian blocks
    while (i + 4 <= len) {
        uint32_t k = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) |
                     ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
        k *= c1; k = murmur_rotl32(k, 15); k *= c2;
        h ^= k; h = murmur_rotl32(h, 13); h = h * 5u + 0xe6546b64u;
        i += 4;
    }

    // tail: the last 1..3 bytes
    uint32_t k = 0;
    switch (len & 3u) {
        case 3: k ^= (uint32_t)data[i + 2] << 16; /* fall through */
        case 2: k ^= (uint32_t)data[i + 1] << 8;  /* fall through */
        case 1: k ^= (uint32_t)data[i];
                k *= c1; k = murmur_rotl32(k, 15); k *= c2; h ^= k;
                break;
        default: break;
    }

    // finalization mix (fmix32)
    h ^= len;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

// Known-answer test: the canonical MurmurHash3 x86_32 vectors, including the classic empty/seed-1
// value and the 0x9747b28c-seeded "aaaa" family used by SMHasher.
int murmur3_selftest(void) {
    static const struct { const char* s; uint32_t seed; uint32_t e; } v[] = {
        {"", 0u, 0x00000000u},
        {"", 1u, 0x514E28B7u},
        {"", 0xFFFFFFFFu, 0x81F16F39u},
        {"test", 0u, 0xBA6BD213u},
        {"Hello, world!", 0u, 0xC0363E43u},
        {"The quick brown fox jumps over the lazy dog", 0u, 0x2E4FF723u},
        {"aaaa", 0x9747b28cu, 0x5A97808Au},
        {"aaa", 0x9747b28cu, 0x283E0130u},
        {"aa", 0x9747b28cu, 0x5D211726u},
        {"a", 0x9747b28cu, 0x7FA09EA6u},
    };
    for (int i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) {
        uint32_t n = 0; while (v[i].s[n]) n++;
        if (murmur3_32((const uint8_t*)v[i].s, n, v[i].seed) != v[i].e) return i + 1;
    }
    return 0;
}
