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
// ed25519_selftest(). Signature verify (needs point decompression) is a follow-up.

void ed25519_pubkey(const uint8_t seed[32], uint8_t pk[32]);

int ed25519_selftest(void);

#endif
