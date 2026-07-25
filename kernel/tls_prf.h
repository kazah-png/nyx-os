#ifndef TLS_PRF_H
#define TLS_PRF_H
// TLS 1.2 pseudo-random function (RFC 5246 §5), P_SHA256. See tls_prf.c.
#include "kernel.h"

// PRF(secret, label, seed) = P_SHA256(secret, label || seed), expanded to outlen bytes.
// `label` is a NUL-terminated ASCII string (e.g. "master secret"); `seed` is raw bytes
// (e.g. client_random || server_random). Writes exactly outlen bytes to out.
void tls12_prf(const uint8_t* secret, uint32_t seclen,
               const char* label,
               const uint8_t* seed, uint32_t seedlen,
               uint8_t* out, uint32_t outlen);

// Run the HMAC-SHA256 (RFC 4231) and TLS 1.2 PRF known-answer vectors; 0 if all pass.
int tls_prf_selftest(void);

#endif
