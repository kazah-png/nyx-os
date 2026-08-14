#include "fnv.h"

// FNV-1a: for each byte, h ^= byte; h *= FNV_prime. Unsigned wraparound IS the
// modulo 2^N, so no explicit mask is needed. 32-bit prime 16777619 (0x01000193);
// 64-bit prime 1099511628211 (0x100000001b3).

uint32_t fnv1a_32_update(uint32_t h, const void* buf, uint32_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (uint32_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

uint64_t fnv1a_64_update(uint64_t h, const void* buf, uint32_t len) {
    const uint8_t* p = (const uint8_t*)buf;
    for (uint32_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x00000100000001b3ull;
    }
    return h;
}

uint32_t fnv1a_32(const void* buf, uint32_t len) { return fnv1a_32_update(FNV1A_32_INIT, buf, len); }
uint64_t fnv1a_64(const void* buf, uint32_t len) { return fnv1a_64_update(FNV1A_64_INIT, buf, len); }

// ---- Known-answer self-test (CI battery) -------------------------------------
// Vectors from the FNV reference test suite, cross-checked against an independent
// Python computation. Also asserts the incremental API chains to the one-shot
// result and that folding an empty chunk leaves an accumulator untouched.
int fnv_selftest(void) {
    struct { const char* s; uint32_t h32; uint64_t h64; } v[] = {
        {"",            0x811c9dc5u, 0xcbf29ce484222325ull},
        {"a",           0xe40c292cu, 0xaf63dc4c8601ec8cull},
        {"b",           0xe70c2de5u, 0xaf63df4c8601f1a5ull},
        {"c",           0xe60c2c52u, 0xaf63de4c8601eff2ull},
        {"foobar",      0xbf9cf968u, 0x85944171f73967e8ull},
        {"hello world", 0xd58b3fa7u, 0x779a65e7023cd2e7ull},
    };
    for (int i = 0; i < 6; i++) {
        uint32_t n = 0; while (v[i].s[n]) n++;
        if (fnv1a_32(v[i].s, n) != v[i].h32) return 1;
        if (fnv1a_64(v[i].s, n) != v[i].h64) return 2;
    }
    // incremental == one-shot: "foo" then "bar" must chain to "foobar"
    uint32_t h = fnv1a_32_update(FNV1A_32_INIT, "foo", 3);
    if (fnv1a_32_update(h, "bar", 3) != 0xbf9cf968u) return 3;
    uint64_t g = fnv1a_64_update(FNV1A_64_INIT, "foo", 3);
    if (fnv1a_64_update(g, "bar", 3) != 0x85944171f73967e8ull) return 4;
    // an empty chunk must not change the accumulator
    if (fnv1a_32_update(0x12345678u, "", 0) != 0x12345678u) return 5;
    if (fnv1a_64_update(0x0123456789abcdefull, "", 0) != 0x0123456789abcdefull) return 6;
    return 0;
}
