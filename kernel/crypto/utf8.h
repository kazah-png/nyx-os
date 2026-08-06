#ifndef UTF8_H
#define UTF8_H

#include "../core/kernel.h"

// Strict UTF-8 (RFC 3629 / Unicode Table 3-7). Decode one code point from s[0..len):
// returns the byte length (1..4) and writes *cp, or 0 on any malformed sequence.
int utf8_decode(const uint8_t* s, size_t len, uint32_t* cp);

// 1 if the whole buffer is well-formed UTF-8, else 0.
int utf8_validate(const uint8_t* s, size_t len);

int utf8_selftest(void);

#endif // UTF8_H
