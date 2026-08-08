#ifndef CHACHA20POLY1305_H
#define CHACHA20POLY1305_H

#include "../core/kernel.h"

/* ChaCha20-Poly1305 AEAD (RFC 8439 §2.8) — authenticated encryption with
 * associated data, composed from the ChaCha20 stream cipher and the Poly1305
 * authenticator already in the tree. The one-time Poly1305 key is ChaCha20
 * block 0; the message is encrypted from block 1; the tag covers
 * aad ‖ pad16 ‖ ciphertext ‖ pad16 ‖ le64(aad_len) ‖ le64(ct_len). This is the
 * cipher suite of TLS 1.3, WireGuard and SSH — a modern alternative to AES-GCM. */

/* Encrypt pt (pt_len bytes) into ct and produce the 16-byte tag over aad+ct.
 * ct must have room for pt_len bytes (may alias pt). Returns 0, or <0 on OOM. */
int chacha20poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                             const uint8_t* aad, uint32_t aad_len,
                             const uint8_t* pt, uint32_t pt_len,
                             uint8_t* ct, uint8_t tag[16]);

/* Verify tag over aad+ct, and only then decrypt ct into pt. Returns 0 on
 * success, 1 on authentication failure (pt left untouched), <0 on OOM. */
int chacha20poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                             const uint8_t* aad, uint32_t aad_len,
                             const uint8_t* ct, uint32_t ct_len,
                             const uint8_t tag[16], uint8_t* pt);

/* Known-answer test over the RFC 8439 §2.8.2 vector + round-trip + tamper. 0 = pass. */
int chacha20poly1305_selftest(void);

#endif
