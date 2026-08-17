#ifndef DEFLATE_H
#define DEFLATE_H
// ============================================================
// deflate.h - DEFLATE (RFC 1951) + zlib (RFC 1950) COMPRESSION
// ============================================================
// The encoder half of inflate.c. A fixed-Huffman + greedy LZ77 compressor (32K hash-chain
// window): small, no heap (one fixed static match window), and self-checking - its output
// round-trips through the very inflate.c that decodes it, and is byte-for-byte accepted by
// stock zlib. Lets NyxOS shrink in-OS data (e.g. packing xbm package sources) now that it can
// already inflate.
#include "../core/kernel.h"

// Compress src[srclen] as a single final fixed-Huffman DEFLATE block into dst[dstcap]. Returns 0
// and sets *outlen on success; -1 if the result would overflow dstcap.
int deflate_raw(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen);

// zlib-wrapped variant: 2-byte header + DEFLATE body + big-endian Adler-32 trailer (RFC 1950) -
// the framing stock zlib (and Python zlib.decompress) expect. Same return contract as deflate_raw.
int zlib_deflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen);

// gzip-wrapped variant (RFC 1952): 10-byte gzip header + DEFLATE body + CRC-32 and ISIZE trailer -
// exactly what stock `gunzip` / Python gzip.decompress read. Same return contract as deflate_raw.
int gzip_deflate(const uint8_t* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap, uint32_t* outlen);
int gzip_selftest(void);

// Known-answer self-test: compress several inputs and require each to inflate back byte-exact via
// inflate.c (raw + zlib framing), plus that compressible inputs actually shrink. 0 = pass.
int deflate_selftest(void);

#endif
