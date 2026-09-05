#ifndef NYX_OD_H
#define NYX_OD_H
// od — dump a byte buffer the way GNU `od` does, byte-for-byte. The pure formatter lives here
// so it can be unit-tested off-target; cmd_od only reads the file(s) and calls it. Supported
// type formats: o1/o2 (octal bytes/words), x1/x2 (hex bytes/words), c (named-escape/char/octal),
// with an octal/hex/decimal/absent address radix, the `*` squeeze of repeated 16-byte lines, and
// a trailing offset line (unless the radix is 'n').
#include "kernel.h"

enum { OD_O1, OD_O2, OD_X1, OD_X2, OD_C };

// Format data[0..len) into out[0..outcap) (NUL-terminated). addr_radix: 'o' | 'x' | 'd' | 'n'.
// type is one of OD_*. Returns the byte count written.
uint32_t od_format(const uint8_t* data, uint32_t len, char addr_radix, int type,
                   char* out, uint32_t outcap);

int od_selftest(void);

#endif
