// ============================================================
// totp.c - RFC 4226 HOTP / RFC 6238 TOTP one-time passwords (v6.4.120)
// ============================================================
// One-time-password generation over the crypto already in-tree: HOTP (RFC 4226) is
// HMAC(secret, counter) run through a dynamic truncation to a short decimal code, and
// TOTP (RFC 6238) is HOTP with counter = floor(unix_time / period). We build both on
// HMAC-SHA-256 (kernel/crypto/sha256.c) and HMAC-SHA-512 (assembled here from the
// streaming SHA-512 API). The shared secret is conventionally Base32-encoded, which is
// exactly why base32 (v6.4.117 kernel / v6.4.119 userland) exists — decode it to raw
// bytes, then call totp_sha256/512. Pinned by totp_selftest() against the RFC 6238
// Appendix B vectors. All-integer (the kernel is -mno-sse, no floats).
#include "../core/kernel.h"
#include "sha256.h"
#include "sha512.h"
#include "sha1.h"
#include "totp.h"

// HMAC-SHA-512 (block size 128 bytes), assembled from the streaming SHA-512 API so no
// concatenation buffers are needed. SHA-256 already ships hmac_sha256().
static void hmac_sha512(const uint8_t* key, uint32_t keylen,
                        const uint8_t* msg, uint32_t msglen, uint8_t out[64]) {
    uint8_t k0[128];
    if (keylen > 128) {                       // long key: K0 = SHA512(key) || zero-pad
        sha512(key, keylen, k0);
        for (int i = 64; i < 128; i++) k0[i] = 0;
    } else {
        for (uint32_t i = 0; i < keylen; i++) k0[i] = key[i];
        for (uint32_t i = keylen; i < 128; i++) k0[i] = 0;
    }
    uint8_t ipad[128], opad[128];
    for (int i = 0; i < 128; i++) { ipad[i] = k0[i] ^ 0x36; opad[i] = k0[i] ^ 0x5c; }

    sha512_ctx_t c;
    uint8_t ihash[64];
    sha512_init(&c); sha512_update(&c, ipad, 128); sha512_update(&c, msg, msglen); sha512_final(&c, ihash);
    sha512_init(&c); sha512_update(&c, opad, 128); sha512_update(&c, ihash, 64);   sha512_final(&c, out);
}

static uint32_t pow10u(int d) { uint32_t p = 1; while (d-- > 0) p *= 10u; return p; }

// RFC 4226 section 5.3 dynamic truncation: the last byte's low nibble selects a 4-byte
// window (offset..offset+3 is always < 20 <= any digest length), mask the high bit, then
// reduce mod 10^digits. Works unchanged for a 32- or 64-byte digest.
static uint32_t dt_truncate(const uint8_t* h, uint32_t hlen, int digits) {
    int off = h[hlen - 1] & 0x0F;
    uint32_t bin = ((uint32_t)(h[off] & 0x7F) << 24) |
                   ((uint32_t)h[off + 1] << 16) |
                   ((uint32_t)h[off + 2] << 8)  |
                   ((uint32_t)h[off + 3]);
    return bin % pow10u(digits);
}

static void be64(uint64_t v, uint8_t out[8]) {
    for (int i = 7; i >= 0; i--) { out[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}

uint32_t hotp_sha256(const uint8_t* key, uint32_t keylen, uint64_t counter, int digits) {
    uint8_t ctr[8]; be64(counter, ctr);
    uint8_t mac[SHA256_DIGEST_SIZE];
    hmac_sha256(key, keylen, ctr, 8, mac);
    return dt_truncate(mac, SHA256_DIGEST_SIZE, digits);
}

uint32_t hotp_sha512(const uint8_t* key, uint32_t keylen, uint64_t counter, int digits) {
    uint8_t ctr[8]; be64(counter, ctr);
    uint8_t mac[64];
    hmac_sha512(key, keylen, ctr, 8, mac);
    return dt_truncate(mac, 64, digits);
}

uint32_t totp_sha256(const uint8_t* key, uint32_t keylen, uint64_t t, uint32_t period, int digits) {
    return hotp_sha256(key, keylen, period ? (t / period) : t, digits);
}

uint32_t totp_sha512(const uint8_t* key, uint32_t keylen, uint64_t t, uint32_t period, int digits) {
    return hotp_sha512(key, keylen, period ? (t / period) : t, digits);
}

// HMAC-SHA-1 is RFC 4226/6238's DEFAULT MAC and what the common authenticator apps
// (Google Authenticator, etc.) actually use, so this is the variant that interoperates
// with real-world 2FA. SHA-1 (kernel/crypto/sha1.c) ships hmac_sha1(); the 20-byte digest
// works unchanged with dt_truncate (its 4-byte window is always within the first 19 bytes).
uint32_t hotp_sha1(const uint8_t* key, uint32_t keylen, uint64_t counter, int digits) {
    uint8_t ctr[8]; be64(counter, ctr);
    uint8_t mac[SHA1_DIGEST_SIZE];
    hmac_sha1(key, keylen, ctr, 8, mac);
    return dt_truncate(mac, SHA1_DIGEST_SIZE, digits);
}

uint32_t totp_sha1(const uint8_t* key, uint32_t keylen, uint64_t t, uint32_t period, int digits) {
    return hotp_sha1(key, keylen, period ? (t / period) : t, digits);
}

// KAT: RFC 6238 Appendix B, 8-digit codes, 30-s period. The SHA-256 seed is the ASCII
// "12345678901234567890123456789012" (32 bytes) and the SHA-512 seed the 64-byte
// "1234...1234"; the expected codes were cross-checked against Python hmac/hashlib.
int totp_selftest(void) {
    const uint8_t* s256 = (const uint8_t*)"12345678901234567890123456789012";
    const uint8_t* s512 = (const uint8_t*)"1234567890123456789012345678901234567890123456789012345678901234";
    struct { uint64_t t; uint32_t c256; uint32_t c512; } v[] = {
        {         59u, 46119246u, 90693936u },
        { 1111111109u, 68084774u, 25091201u },
        { 1111111111u, 67062674u, 99943326u },
        { 1234567890u, 91819424u, 93441116u },
        { 2000000000u, 90698825u, 38618901u },
        {20000000000u, 77737706u, 47863826u },
    };
    for (int i = 0; i < 6; i++) {
        if (totp_sha256(s256, 32, v[i].t, 30, 8) != v[i].c256) return 1;
        if (totp_sha512(s512, 64, v[i].t, 30, 8) != v[i].c512) return 2;
    }
    // HOTP relationship: TOTP(t) must equal HOTP(floor(t/period)).
    if (hotp_sha256(s256, 32, 59u / 30u, 8) != 46119246u) return 3;
    // Sensitivity: a one-second-later time in the SAME 30-s window is unchanged, but a
    // different window, a different secret and fewer digits all change the code.
    if (totp_sha256(s256, 32, 59u, 30, 8) != totp_sha256(s256, 32, 45u, 30, 8)) return 4;   // same window (t/30==1)
    if (totp_sha256(s256, 32, 59u, 30, 8) == totp_sha256(s256, 32, 89u, 30, 8)) return 5;   // next window
    if (totp_sha256(s256, 32, 59u, 30, 8) == totp_sha256((const uint8_t*)"12345678901234567890123456789013", 32, 59u, 30, 8)) return 6;
    if ((totp_sha256(s256, 32, 59u, 30, 8) % 1000000u) != totp_sha256(s256, 32, 59u, 30, 6)) return 7;  // 6-digit = low 6 of 8

    // RFC 6238 SHA-1 variant — the 20-byte seed "12345678901234567890" and the DEFAULT
    // HMAC of the authenticator ecosystem. (07081804 is stored as the integer 7081804 —
    // the leading zero is only the 8-digit display padding.)
    const uint8_t* s1 = (const uint8_t*)"12345678901234567890";
    struct { uint64_t t; uint32_t c1; } v1[] = {
        {         59u, 94287082u },
        { 1111111109u,  7081804u },
        { 1111111111u, 14050471u },
        { 1234567890u, 89005924u },
        { 2000000000u, 69279037u },
        {20000000000u, 65353130u },
    };
    for (int i = 0; i < 6; i++)
        if (totp_sha1(s1, 20, v1[i].t, 30, 8) != v1[i].c1) return 8;

    return 0;
}
