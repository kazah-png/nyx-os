#ifndef AES_GCM_H
#define AES_GCM_H
// AES-128 in GCM (AEAD) — the record protection for TLS_*_AES_128_GCM_SHA256. See aes_gcm.c.
#include "../core/kernel.h"

// AEAD seal: encrypt pt[pt_len] -> ct[pt_len] and produce the 16-byte auth tag over
// aad[aad_len] + ct. iv is the 12-byte GCM nonce.
void aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* pt, uint32_t pt_len,
                        uint8_t* ct, uint8_t tag[16]);

// AEAD open: verify the tag over aad + ct, and only on success decrypt ct -> pt.
// Returns 0 if the tag is valid (pt filled), -1 if not (pt left untouched).
int  aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                        const uint8_t* aad, uint32_t aad_len,
                        const uint8_t* ct, uint32_t ct_len,
                        const uint8_t tag[16], uint8_t* pt);

// Run the FIPS-197 AES block vector and the NIST/McGrew-Viega GCM vectors; 0 if all pass.
int  aes_gcm_selftest(void);

#endif
