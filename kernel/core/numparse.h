#ifndef NUMPARSE_H
#define NUMPARSE_H
#include "kernel.h"

// Strict base-10 integer parsers with overflow detection. Unlike atoi()/strtol() in
// kernel.h — which silently return 0 on non-numeric input, silently overflow (undefined
// behaviour) on a value too large for the type, and give no way to tell "0" apart from
// garbage — these validate the WHOLE string and report failure explicitly. On success
// they write *out and return 0; on any error (NULL, empty, a lone sign, a non-digit,
// trailing junk, or a value outside the type's range) they return -1 and leave *out
// untouched. Optional leading/trailing ASCII blanks are allowed, as is one leading '+'
// (and '-' for the signed form) — but no blank may sit between the sign and the digits.
int parse_i64(const char* s, int64_t* out);
int parse_u64(const char* s, uint64_t* out);

// Parse an unsigned decimal and require it to be <= max (e.g. a TCP port <= 65535).
// Rejects everything parse_u64 rejects, plus any value over max. This is the safe
// replacement for the `(uint16_t)atoi(arg)` idiom, which silently truncates an
// out-of-range port to a DIFFERENT port (atoi("99999") -> 34463) instead of erroring.
int parse_u32_max(const char* s, uint32_t max, uint32_t* out);

int numparse_selftest(void);   // KAT: boundary values + adversarial overflow/junk rejection

#endif
