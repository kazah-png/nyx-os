#ifndef P384_H
#define P384_H
// NIST P-384 (secp384r1) field + point arithmetic. See p384.c.
#include "kernel.h"

// out = k * G (the base point). k, x, y are 48-byte big-endian. Returns 0.
int p384_base_scalar(const uint8_t k[48], uint8_t x[48], uint8_t y[48]);

// out = k * (px, py). Points are 48-byte big-endian affine coordinates. Returns 0 on
// success, -1 if the input point is malformed (at infinity).
int p384_point_scalar(const uint8_t k[48], const uint8_t px[48], const uint8_t py[48],
                      uint8_t x[48], uint8_t y[48]);

// ECDSA-P384 verify: 0 if signature (r,s) over `hash` is valid under public key (Qx,Qy),
// -1 otherwise. All inputs are 48-byte big-endian.
int p384_ecdsa_verify(const uint8_t Qx[48], const uint8_t Qy[48], const uint8_t hash[48],
                      const uint8_t r[48], const uint8_t s[48]);

// Known-answer self-test: NIST P-384 base-point multiples + an ECDSA verify. 0 if all pass.
int p384_selftest(void);

#endif
