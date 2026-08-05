#ifndef NYX_BASE64_H
#define NYX_BASE64_H
// Standard Base64 (RFC 4648, '+' '/' alphabet, '=' padding). A small, dependency-
// free codec for the crypto/PEM neighbourhood (PEM certificate bodies, HTTP Basic
// credentials, data: URIs). The decoder is deliberately STRICT — it rejects
// anything a lenient decoder would silently accept, so malformed input can never
// smuggle bytes through.
#include "../core/kernel.h"

// Bytes the encoder writes (excluding the NUL terminator) for `n` input bytes.
static inline uint32_t base64_encoded_len(uint32_t n) { return ((n + 2) / 3) * 4; }

// Encode in[in_len] into a NUL-terminated Base64 string. `out` must hold at least
// base64_encoded_len(in_len)+1 bytes. Returns the string length (excluding NUL).
uint32_t base64_encode(const uint8_t* in, uint32_t in_len, char* out);

// STRICT decode of in[in_len]. Rejects (returns -1, nothing usable written):
//   - a length that is not a multiple of 4,
//   - any byte outside [A-Za-z0-9+/] or a '=' that is not trailing padding,
//   - padding anywhere but the final quartet, or "=" before a data char,
//   - non-canonical trailing bits (the unused low bits of the last data symbol
//     must be zero) — the malleability check most decoders skip.
// On success writes the bytes to `out` (at most in_len/4*3), sets *out_len, and
// returns 0. Pass in_len == 0 for the empty string (*out_len = 0, returns 0).
int base64_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len);

#endif
