#ifndef LEB128_H
#define LEB128_H
#include "kernel.h"

// LEB128 — variable-length integer coding (the "varint" used by DWARF, WebAssembly,
// Android DEX, protobuf, ...). Little-endian base-128: each byte carries 7 payload bits
// low-first, with the high bit a "more bytes follow" continuation flag. Unsigned and
// signed (two's-complement, sign-extended) variants. Every DECODER is bounds-checked: it
// never reads past `inlen`, rejects a truncated value (continuation set but input ends),
// and rejects one that would not fit in 64 bits — so it is safe on untrusted input.
// Pinned by leb128_selftest().

int uleb128_encode(uint64_t v, uint8_t* out, int outcap);          // -> bytes written, or -1 (no room)
int uleb128_decode(const uint8_t* in, int inlen, uint64_t* out);   // -> bytes read, or -1 (truncated / >64-bit)
int sleb128_encode(int64_t v, uint8_t* out, int outcap);           // -> bytes written, or -1 (no room)
int sleb128_decode(const uint8_t* in, int inlen, int64_t* out);    // -> bytes read, or -1 (truncated / overlong)

int leb128_selftest(void);   // KAT: canonical vectors + round-trip + malformed rejections

#endif
