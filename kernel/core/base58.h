#ifndef NYX_BASE58_H
#define NYX_BASE58_H
// Base58 (Bitcoin alphabet: no 0/O/I/l). Encodes a byte string as one big-endian base-58
// number; every leading 0x00 byte is rendered as a leading '1' so leading zeros survive the
// round trip. The night-goddess's coreutil sibling of base16/32/64/85, and the encoding
// behind crypto addresses. HARDENED: bounded work buffers (no allocation, no recursion),
// invalid decode characters are rejected, and every write is capacity-checked.
#include "kernel.h"

#define BASE58_MAX 256   // max input bytes (encode) / output bytes (decode)

// Encode in[0..inlen) into out (NUL-terminated). Returns the encoded length (excluding the
// NUL), or -1 if inlen > BASE58_MAX or the result would not fit in outcap.
int base58_encode(const uint8_t* in, uint32_t inlen, char* out, uint32_t outcap);

// Decode in[0..inlen) into out. Returns the decoded byte count, or -1 on an invalid
// character or if the result would exceed outcap / BASE58_MAX.
int base58_decode(const char* in, uint32_t inlen, uint8_t* out, uint32_t outcap);

int base58_selftest(void);   // 0 = pass; otherwise the failing vector index (1-based)
#endif
