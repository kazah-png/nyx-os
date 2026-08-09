#ifndef AES_CTR_H
#define AES_CTR_H
// AES-128 in CTR mode (NIST SP 800-38A §6.5) — the standard confidentiality-only stream
// mode: E_K(counter) is a keystream XORed with the data, and the 128-bit counter block is
// incremented (big-endian) per block. Encryption and decryption are the same operation.
// Complements aes_gcm.c (AEAD) and aes_cmac.c (MAC) — all three share the FIPS-197 block
// exposed from aes_gcm.h, no second copy. See aes_ctr.c.
#include "../core/kernel.h"

// XOR crypt in-place-safe: out[len] = in[len] ^ AES128-CTR keystream(key, counter). The
// 16-byte counter is the initial counter block (unchanged in the caller — a local copy is
// advanced). len may be any size; the final partial block uses only its leading bytes.
void aes128_ctr_xcrypt(const uint8_t key[16], const uint8_t counter[16],
                       const uint8_t* in, uint8_t* out, uint32_t len);

// Known-answer test over the NIST SP 800-38A §F.5 CTR-AES128 vectors; 0 if all pass.
int  aes_ctr_selftest(void);

#endif // AES_CTR_H
