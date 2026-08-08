#ifndef AES_CMAC_H
#define AES_CMAC_H
// AES-CMAC (RFC 4493 / NIST SP 800-38B) — a block-cipher-based MAC over AES-128.
// The OMAC1 construction: two GF(2^128) subkeys K1/K2 derived from E_K(0), the message
// CBC-MAC'd, and the final block masked by K1 (whole) or padded+K2 (partial). See aes_cmac.c.
#include "../core/kernel.h"

// Compute the 16-byte CMAC of msg[len] under the 16-byte key. len may be 0.
void aes128_cmac(const uint8_t key[16], const uint8_t* msg, uint32_t len, uint8_t mac[16]);

// Known-answer test over the four RFC 4493 §4 vectors; returns 0 if all pass.
int  aes_cmac_selftest(void);

#endif
