#ifndef CSPRNG_H
#define CSPRNG_H
// Cryptographically-secure RNG (HMAC_DRBG-SHA256, NIST SP 800-90A). See csprng.c.
#include "kernel.h"

// Fill out[0..n) with cryptographically-strong random bytes. The DRBG is instantiated on
// first use from the best available hardware entropy (RDSEED/RDRAND, plus RDTSC jitter and
// the tick counter) and periodically reseeded.
void csprng_bytes(uint8_t* out, uint32_t n);

// Known-answer self-test of the HMAC_DRBG core against a NIST-format vector; 0 if it passes.
int csprng_selftest(void);

#endif
