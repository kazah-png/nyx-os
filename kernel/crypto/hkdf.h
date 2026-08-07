#ifndef HKDF_H
#define HKDF_H

#include "../core/kernel.h"

/* HKDF — HMAC-based Extract-and-Expand Key Derivation Function (RFC 5869),
 * instantiated over HMAC-SHA-256 (HashLen = 32). Turns a high-entropy but
 * non-uniform input (a DH shared secret, a master key) into any number of
 * independent, uniformly-random output keys — the key-schedule primitive behind
 * TLS 1.3. Distinct from PBKDF2 (which stretches a low-entropy password). */

#define HKDF_HASHLEN  32
#define HKDF_MAX_INFO 512   /* internal working-buffer bound on the info string */

/* PRK = HMAC-SHA256(salt, IKM). A NULL or empty salt becomes HashLen zero bytes
 * (RFC 5869 §2.2). prk must have room for 32 bytes. */
void hkdf_extract(const uint8_t* salt, uint32_t saltlen,
                  const uint8_t* ikm,  uint32_t ikmlen,
                  uint8_t prk[HKDF_HASHLEN]);

/* OKM = T(1) | T(2) | … truncated to okmlen, where T(0) is empty and
 * T(i) = HMAC-SHA256(PRK, T(i-1) | info | i). Returns 0 on success, 1 if okmlen
 * exceeds 255*HashLen or infolen exceeds HKDF_MAX_INFO (the RFC / buffer bounds). */
int hkdf_expand(const uint8_t prk[HKDF_HASHLEN],
                const uint8_t* info, uint32_t infolen,
                uint8_t* okm, uint32_t okmlen);

/* Extract then Expand in one call. Same return contract as hkdf_expand. */
int hkdf(const uint8_t* salt, uint32_t saltlen,
         const uint8_t* ikm,  uint32_t ikmlen,
         const uint8_t* info, uint32_t infolen,
         uint8_t* okm, uint32_t okmlen);

/* Known-answer test over the RFC 5869 Appendix A SHA-256 vectors. 0 = pass. */
int hkdf_selftest(void);

#endif
