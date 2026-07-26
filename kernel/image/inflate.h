#ifndef INFLATE_H
#define INFLATE_H
// DEFLATE (RFC 1951) + zlib (RFC 1950) decompression. See inflate.c. This is the decompression
// core PNG's IDAT stream needs; the first step toward decoding real images in Selene.
#include "../core/kernel.h"

// Raw DEFLATE decompress src[srclen] into dst[dstcap]. On success returns 0 and sets *outlen to
// the number of bytes written; on failure returns a negative error code (bad stream / overflow).
int inflate_raw(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen);

// zlib-wrapped DEFLATE (2-byte header + DEFLATE body + trailing Adler-32, which is verified).
// This is the framing PNG IDAT uses. Same return contract as inflate_raw.
int zlib_inflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen);

// Known-answer self-test (`deflatetest`): fixed / dynamic / stored blocks + a zlib+Adler-32 case.
int inflate_selftest(void);

#endif
