#ifndef NYX_URLCODEC_H
#define NYX_URLCODEC_H
// Percent-encoding (RFC 3986) - the %XX transfer encoding used in URLs and in
// x-www-form-urlencoded bodies. This is the shared, hardened codec: the unreserved
// set A-Z a-z 0-9 - . _ ~ passes through untouched, every other byte becomes %XX with
// UPPERCASE hex, and the DECODER is STRICT - a '%' that is not followed by exactly two
// hex digits is REJECTED, so malformed/hostile input fails cleanly instead of silently
// mangling (the kind of leniency that hides request-smuggling bugs). Byte-for-byte
// compatible with Python urllib.parse.quote(s, safe='') / unquote for well-formed input.
// '+' is a LITERAL here (not space) - the form-body '+'-for-space convention is a
// caller concern, not part of the RFC 3986 codec.
#include "kernel.h"

// Encode src[srclen] into dst as a NUL-terminated string. Worst case is 3*srclen+1
// bytes. Returns the encoded length (excluding the NUL), or -1 if dst is too small.
int url_pct_encode(const uint8_t* src, uint32_t srclen, char* dst, uint32_t dstcap);

// STRICT decode of src[srclen] into dst. Returns the decoded length, or -1 on a
// malformed escape ('%' without two following hex digits) or if dst is too small.
int url_pct_decode(const char* src, uint32_t srclen, uint8_t* dst, uint32_t dstcap);

int url_codec_selftest(void);
#endif
