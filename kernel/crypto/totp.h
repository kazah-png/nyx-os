#ifndef TOTP_H
#define TOTP_H
#include "../core/kernel.h"

// RFC 4226 HOTP / RFC 6238 TOTP one-time passwords, built on the in-tree HMAC-SHA-256
// and HMAC-SHA-512. The moving factor `counter` is an 8-byte big-endian value; for TOTP
// it is floor(unix_time / period) (period is typically 30 s). `digits` is the code
// length (1..9; 6 or 8 are the usual choices). The shared secret is by convention
// Base32-encoded (see kernel/crypto/base32.c) — decode it before passing the raw key.
uint32_t hotp_sha256(const uint8_t* key, uint32_t keylen, uint64_t counter, int digits);
uint32_t hotp_sha512(const uint8_t* key, uint32_t keylen, uint64_t counter, int digits);
uint32_t totp_sha256(const uint8_t* key, uint32_t keylen, uint64_t unix_time,
                     uint32_t period, int digits);
uint32_t totp_sha512(const uint8_t* key, uint32_t keylen, uint64_t unix_time,
                     uint32_t period, int digits);

int totp_selftest(void);   // KAT against the RFC 6238 Appendix B test vectors

#endif
