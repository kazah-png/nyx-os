#ifndef SHA3_H
#define SHA3_H
// SHA3-256 (FIPS 202) — the Keccak sponge hash. A different construction from every other
// hash in the tree: SHA-2 (sha256/sha512) is Merkle-Damgard, BLAKE2s is an ARX chain, and
// this is a sponge over the Keccak-f[1600] permutation. See sha3.c.
#include "../core/kernel.h"

// Compute the 32-byte SHA3-256 digest of in[inlen] into out.
void sha3_256(const uint8_t* in, uint32_t inlen, uint8_t out[32]);

// Known-answer test over the FIPS 202 / NIST SHA3-256 vectors; returns 0 if all pass.
int  sha3_selftest(void);

#endif // SHA3_H
