#ifndef AES_KW_H
#define AES_KW_H

#include "../core/kernel.h"

// AES Key Wrap (RFC 3394) with a 128-bit KEK. A deterministic, integrity-checked way
// to encrypt ("wrap") key material for storage or transport under a key-encryption key:
// the unwrap verifies the RFC 3394 IV and fails closed on any tampering, so a wrapped
// key can be kept on untrusted media. NyxOS had AEAD (GCM, ChaCha20-Poly1305), a MAC
// (CMAC), hashing and KDFs but no key-wrap primitive; this fills that gap. Built on the
// shared AES-128 block cipher (aes_gcm.h / aes_cbc.c). Pinned by aes_kw_selftest().

// Wrap `nblk` 64-bit blocks (nblk >= 2, i.e. keys of 128 bits or more) from `in`
// (nblk*8 bytes) under `kek`, writing (nblk+1)*8 bytes to `out`. Returns 0, or -1 if
// nblk < 2.
int aes128_kw_wrap(const uint8_t kek[16], const uint8_t* in, uint32_t nblk, uint8_t* out);

// Unwrap (nblk+1)*8 bytes from `in` into nblk*8 bytes at `out` under `kek`. Returns 0
// on success, or -1 if the integrity check (the recovered IV) fails or nblk < 2. On
// failure `out` holds the (untrusted) candidate plaintext — callers must ignore it.
int aes128_kw_unwrap(const uint8_t kek[16], const uint8_t* in, uint32_t nblk, uint8_t* out);

int aes_kw_selftest(void);   // KAT: RFC 3394 §4.1 vector + round-trip + tamper-reject

#endif
