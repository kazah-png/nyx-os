#ifndef BLAKE2S_H
#define BLAKE2S_H

#include "../core/kernel.h"

/* BLAKE2s (RFC 7693) — a fast, modern cryptographic hash built on the ChaCha
 * quarter-round (ARX: add-rotate-xor, no tables, constant-time by construction).
 * This is the unkeyed 256-bit variant (BLAKE2s-256), a drop-in stronger/faster
 * alternative to the SHA-256 already in the tree. */
void blake2s256(const uint8_t* in, uint32_t inlen, uint8_t out[32]);

/* Keyed BLAKE2s-256 — a MAC: the key (1..32 bytes) is prepended as a padded
 * block, so a shared key authenticates the message. keylen 0 == the plain hash. */
void blake2s256_keyed(const uint8_t* key, uint32_t keylen,
                      const uint8_t* in, uint32_t inlen, uint8_t out[32]);

/* Known-answer test against reference BLAKE2s-256 vectors. 0 = pass. */
int blake2s_selftest(void);

#endif
