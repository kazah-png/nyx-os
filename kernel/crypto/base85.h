#ifndef NYX_BASE85_H
#define NYX_BASE85_H
// Ascii85 (a.k.a. btoa / Adobe ASCII85): five printable chars ('!'..'u') per 4 bytes,
// with a 'z' shorthand for an all-zero group and partial final groups trimmed. The
// densest of the base-N family (base16/32/64) — 4 bytes -> 5 chars (~25% overhead vs
// Base64's ~33%). Byte-for-byte compatible with Python base64.a85encode/a85decode
// (defaults). The decoder is STRICT: it rejects out-of-range groups, a stray 'z', a
// lone trailing char, and any non-alphabet byte.
#include "../core/kernel.h"

// Upper bound on encoder output bytes (excluding NUL) for `n` input bytes — the no-'z'
// worst case: 5 per full group + (r+1) for an r-byte remainder. Safe for sizing `out`.
static inline uint32_t base85_encoded_len(uint32_t n) {
    return 5 * (n / 4) + (n % 4 ? (n % 4) + 1 : 0);
}

// Encode in[in_len] into a NUL-terminated Ascii85 string. `out` must hold at least
// base85_encoded_len(in_len)+1 bytes. Returns the string length (excluding NUL).
uint32_t base85_encode(const uint8_t* in, uint32_t in_len, char* out);

// STRICT decode of in[in_len]. Rejects (returns -1, nothing usable written):
//   - a byte outside '!'..'u' (other than 'z'),
//   - a 'z' that is not on a group boundary,
//   - a single leftover char at the end (a group needs >= 2 chars),
//   - a 5-char group whose 32-bit value exceeds 2^32-1 (overflow).
// On success writes the bytes to `out`, sets *out_len, returns 0. in_len == 0 -> empty.
int base85_decode(const char* in, uint32_t in_len, uint8_t* out, uint32_t* out_len);

int base85_selftest(void);
#endif
