#ifndef TAR_H
#define TAR_H

#include "../core/kernel.h"

// Minimal, hardened POSIX ustar (tar) reader — enough to LIST an archive's members.
// A tar file is a sequence of 512-byte headers, each followed by the member's data
// padded up to 512 bytes; the stream ends at a zero-filled block. This parser only
// reads headers (name, size, type) and never trusts a field past the buffer.

typedef struct {
    char     name[101];   // member path (ustar name field, <=100 bytes, NUL-terminated)
    uint32_t size;        // member size in bytes (parsed from the octal size field)
    char     type;        // typeflag: '0'/'\0' regular, '5' directory, etc.
} tar_entry_t;

// Parse the ustar header at *off. On success fill *e, advance *off to the next header,
// and return 1. Return 0 at the end of the archive (a zero block, a missing "ustar"
// magic, or not enough room for a full 512-byte header). Every access is bounded by len.
int tar_next(const uint8_t* data, uint32_t len, uint32_t* off, tar_entry_t* e);

int tar_selftest(void);

#endif // TAR_H
