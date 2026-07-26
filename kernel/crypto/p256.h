#ifndef P256_H
#define P256_H
// NIST P-256 (secp256r1) field + point arithmetic. See p256.c.
#include "../core/kernel.h"

// out = k * G (the base point). k, x, y are 32-byte big-endian. Returns 0.
int p256_base_scalar(const uint8_t k[32], uint8_t x[32], uint8_t y[32]);

// out = k * (px, py). Points are 32-byte big-endian affine coordinates. Returns 0 on
// success, -1 if the input point is malformed (at infinity).
int p256_point_scalar(const uint8_t k[32], const uint8_t px[32], const uint8_t py[32],
                      uint8_t x[32], uint8_t y[32]);

// ECDSA-P256 verify: 0 if signature (r,s) over `hash` is valid under public key (Qx,Qy),
// -1 otherwise. All inputs are 32-byte big-endian.
int p256_ecdsa_verify(const uint8_t Qx[32], const uint8_t Qy[32], const uint8_t hash[32],
                      const uint8_t r[32], const uint8_t s[32]);

// Known-answer self-test: NIST P-256 base-point multiples + an ECDSA verify. 0 if all pass.
int p256_selftest(void);

#endif
