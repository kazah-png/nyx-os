#ifndef NYX_BASE32_H
#define NYX_BASE32_H
// Standard Base32 (RFC 4648 §6, the "A-Z 2-7" alphabet, '=' padding). A small,
// dependency-free codec — Base32 is the encoding used for TOTP/2FA secrets, some
// tokens, and case-insensitive/DNS-safe contexts. Like base64.h, the decoder is
// deliberately STRICT: it rejects anything a lenient decoder would silently accept,
// so malformed input can never smuggle bytes through.
#include "../core/kernel.h"

// Bytes the encoder writes (excluding the NUL terminator) for `n` input bytes.
static inline uint32_t base32_encoded_len(uint32_t n) { return ((n + 4) / 5) * 8; }

// Encode in[in_len] into a NUL-terminated Base32 string. `out` must hold at least
// base32_encoded_len(in_len)+1 bytes. Returns the string length (excluding NUL).
uint32_t base32_encode(const uint8_t* in, uint32_t in_len, char* out);

// STRICT decode of in[in_len]. Rejects (returns -1, nothing usable written):
//   - a length that is not a multiple of 8,
//   - any byte outside [A-Z2-7] or a '=' that is not trailing padding of the group,
//   - a padding count other than 0/1/3/4/6 (the only ones a real byte length yields),
//   - padding in a non-final group,
//   - non-canonical trailing bits (the unused low bits of the last data symbol must
//     be zero) — the malleability check most decoders skip.
// On success writes the bytes to `out` (at most in_len/8*5), sets *out_len, returns 0.
// Pass in_len == 0 for the empty string (*out_len = 0, returns 0).
int base32_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len);

int base32_selftest(void);

#endif
