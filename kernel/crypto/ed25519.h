#ifndef ED25519_H
#define ED25519_H

#include "../core/kernel.h"

// Ed25519 (RFC 8032) — signature primitive over the twisted-Edwards curve on the same
// field GF(2^255-19) as X25519. This first step derives the 32-byte PUBLIC KEY from a
// 32-byte seed (private key): SHA-512 of the seed, clamp, and a fixed-base scalar
// multiply of the Edwards base point, then compress. EdDSA gives NyxOS a way to VERIFY
// signed data (packages, updates) under a trusted public key — the trust primitive
// X25519 (key exchange) cannot provide. This is the compact public-domain TweetNaCl
// construction (its field/point ops), reusing the kernel's SHA-512. Pinned by
// ed25519_selftest().
//
// ed25519_verify() checks a 64-byte detached signature (R‖S) over a message under a
// trusted public key — point decompression + the group equation [S]B == R + [h]A,
// h = SHA-512(R‖A‖M) mod L — returning 0 iff valid, with a constant-time final
// compare. This is the actual trust operation (verifying signed packages/updates).
// Pinned by ed25519_verify_selftest() against the RFC 8032 §7.1 vectors.

void ed25519_pubkey(const uint8_t seed[32], uint8_t pk[32]);
int  ed25519_verify(const uint8_t sig[64], const uint8_t* msg, uint32_t mlen, const uint8_t pk[32]);

int ed25519_selftest(void);
int ed25519_verify_selftest(void);

#endif
