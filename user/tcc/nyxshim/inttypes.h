#ifndef NYX_TCC_INTTYPES_H
#define NYX_TCC_INTTYPES_H
/* NyxOS tcc-port shim for <inttypes.h>: the fixed-width types (via <stdint.h>) plus
 * the printf/scanf length-modifier macros tcc's elf.h and diagnostics use. LP64, so
 * a 64-bit value is `long` -> "l". Keeps the port host-header-free for the in-OS
 * self-host build. */
#include <stdint.h>

#define PRId8  "d"
#define PRIi8  "i"
#define PRIu8  "u"
#define PRIx8  "x"
#define PRId16 "d"
#define PRIi16 "i"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRId32 "d"
#define PRIi32 "i"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define PRIdPTR "ld"
#define PRIiPTR "li"
#define PRIuPTR "lu"
#define PRIxPTR "lx"

#endif
