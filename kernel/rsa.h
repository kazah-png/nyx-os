#ifndef RSA_H
#define RSA_H
// RSA PKCS#1 v1.5 signature verification over SHA-256. See rsa.c.
#include "kernel.h"

// Verify sig against hash under public key (N, e). N and sig are big-endian, e is the small
// public exponent (e.g. 65537). Returns 0 if the signature is valid, -1 otherwise.
// RSASSA-PSS verify (SHA-256, MGF1-SHA256, 32-byte salt = rsa_pss_rsae_sha256). 0 if valid.
int rsa_pss_sha256_verify(const uint8_t* N, uint32_t Nlen, uint64_t e,
                          const uint8_t* sig, uint32_t siglen, const uint8_t mHash[32]);

int rsa_pkcs1_sha256_verify(const uint8_t* N, uint32_t Nlen, uint64_t e,
                            const uint8_t* sig, uint32_t siglen, const uint8_t hash[32]);

// Known-answer self-test (valid signature accepted, tampered hash/sig rejected); 0 if all pass.
int rsa_selftest(void);

#endif
