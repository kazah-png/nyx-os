#ifndef AES_CBC_H
#define AES_CBC_H
// AES-128-CBC (NIST SP 800-38A §6.2). The chaining block-cipher mode — each plaintext
// block is XORed with the previous ciphertext block (the first with the IV) before it is
// encrypted, so identical plaintext blocks encrypt differently and a bit flip propagates.
// This is the first mode in the tree that needs AES *decryption*: GCM/CTR/CMAC are all
// built on encrypt-only AES (CTR is symmetric), but CBC decrypt runs the real FIPS-197
// inverse cipher, which aes_cbc.c adds (reusing the forward key schedule from aes_gcm.h).
// Length must be a whole number of 16-byte blocks (the caller owns any padding).
#include "../core/kernel.h"

// Encrypt / decrypt `len` bytes (a multiple of 16) under key + iv. in and out may alias.
// Returns 0 on success, -1 if len is not a multiple of the 16-byte block size.
int aes128_cbc_encrypt(const uint8_t key[16], const uint8_t iv[16],
                       const uint8_t* in, uint32_t len, uint8_t* out);
int aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                       const uint8_t* in, uint32_t len, uint8_t* out);

// Known-answer test against the NIST SP 800-38A F.2 CBC-AES128 vectors (+ round-trip,
// in-place aliasing, and a bad-length rejection). Returns 0 if all pass.
int aes_cbc_selftest(void);

#endif // AES_CBC_H
