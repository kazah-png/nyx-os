#ifndef NYX_BASE16_H
#define NYX_BASE16_H
// Standard Base16 / hex (RFC 4648 §8): the "0-9A-F" alphabet, two symbols per byte.
// The most common encoding for keys, hashes and MAC addresses. Like base32.h /
// base64.h, the decoder is STRICT — it rejects an odd length and any non-hex symbol
// (including embedded whitespace), so malformed hex can never smuggle bytes through.
// Encoding is UPPERCASE per the RFC; decoding accepts either case.
#include "../core/kernel.h"

// Bytes the encoder writes (excluding the NUL terminator) for `n` input bytes.
static inline uint32_t base16_encoded_len(uint32_t n) { return n * 2; }

// Encode in[in_len] into a NUL-terminated UPPERCASE hex string. `out` must hold at
// least in_len*2+1 bytes. Returns the string length (in_len*2).
uint32_t base16_encode(const uint8_t* in, uint32_t in_len, char* out);

// STRICT decode of in[in_len]. Rejects (returns -1, nothing usable written):
//   - an odd length,
//   - any symbol outside [0-9A-Fa-f].
// On success writes in_len/2 bytes to `out`, sets *out_len, returns 0.
// Pass in_len == 0 for the empty string (*out_len = 0, returns 0).
int base16_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len);

int base16_selftest(void);

#endif
