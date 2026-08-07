#ifndef CHACHA20_H
#define CHACHA20_H

#include "../core/kernel.h"

/* ChaCha20 stream cipher (RFC 8439): 256-bit key, 96-bit nonce, 32-bit block
 * counter. An ARX design (add-rotate-xor) with no S-box lookups, so it is
 * constant-time by construction — the stream half of the ChaCha20-Poly1305
 * AEAD used by TLS 1.3, WireGuard and SSH, and a modern complement to the
 * table-driven AES-GCM already in the tree. */

/* One 64-byte keystream block for (key, counter, nonce). */
void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]);

/* out = in XOR keystream, starting at block `counter`. in and out may alias;
 * len is in bytes. Encryption and decryption are the same operation. */
void chacha20_xor(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12],
                  const uint8_t* in, uint8_t* out, uint32_t len);

/* Known-answer test over the RFC 8439 vectors. 0 = pass. */
int chacha20_selftest(void);

#endif
