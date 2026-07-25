#ifndef CURVE25519_H
#define CURVE25519_H
// X25519 (Curve25519 ECDH) — RFC 7748. See curve25519.c.
#include "kernel.h"

// out = scalar * point  (both little-endian 32-byte encodings of u-coordinates).
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);

// out = scalar * basepoint (u = 9): derive a public key from a private scalar.
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

// Run the RFC 7748 known-answer vectors; return 0 if all pass, -1 otherwise.
int curve25519_selftest(void);

#endif
