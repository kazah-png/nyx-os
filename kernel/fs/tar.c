// ============================================================
// tar.c - hardened POSIX ustar reader (see tar.h). Lists an archive's members
// without ever trusting a length or magic beyond the supplied buffer.
// ============================================================
#include "tar.h"

int tar_next(const uint8_t* data, uint32_t len, uint32_t* off, tar_entry_t* e) {
    uint32_t o = *off;
    if (o + 512u > len || o + 512u < o) return 0;      // no room for a header (or overflow)
    const uint8_t* h = data + o;

    if (h[0] == 0) return 0;                            // a zero block ends the archive
    // ustar magic at offset 257 — POSIX "ustar\0" or GNU "ustar  "; require the 5 letters.
    if (!(h[257] == 'u' && h[258] == 's' && h[259] == 't' &&
          h[260] == 'a' && h[261] == 'r')) return 0;

    int i = 0;                                          // name (<=100, NUL-terminated)
    for (; i < 100 && h[i]; i++) e->name[i] = (char)h[i];
    e->name[i] = '\0';

    uint32_t sz = 0;                                    // size: octal in [124, 136)
    for (int j = 124; j < 136; j++) {
        uint8_t c = h[j];
        if (c >= '0' && c <= '7') sz = sz * 8u + (uint32_t)(c - '0');
        else if (c == ' ' && sz == 0) continue;         // leading pad spaces
        else break;                                     // trailing NUL/space ends the field
    }
    e->size = sz;
    e->type = (char)h[156];

    uint32_t blocks = (sz + 511u) / 512u;               // data is padded to a 512 multiple
    uint32_t next = o + 512u + blocks * 512u;
    if (next <= o || next > len) { *off = len; return 1; }  // last entry; next call ends
    *off = next;
    return 1;
}

// ---- known-answer self-test (`tar`) ----
int tar_selftest(void) {
    static uint8_t arc[1024];
    for (int i = 0; i < 1024; i++) arc[i] = 0;
    const char* nm = "hello.txt";                       // one regular-file member, 5 bytes
    for (int i = 0; nm[i]; i++) arc[i] = (uint8_t)nm[i];
    const char* so = "00000000005";                     // size = 05 octal = 5
    for (int i = 0; so[i]; i++) arc[124 + i] = (uint8_t)so[i];
    arc[156] = '0';                                     // typeflag: regular file
    const char* mg = "ustar";
    for (int i = 0; mg[i]; i++) arc[257 + i] = (uint8_t)mg[i];
    const char* dt = "world";                           // its data in block 1
    for (int i = 0; dt[i]; i++) arc[512 + i] = (uint8_t)dt[i];

    tar_entry_t e;
    uint32_t off = 0;
    if (!tar_next(arc, 1024, &off, &e)) return 1;        // must parse the member
    for (int i = 0; i < 10; i++) if (e.name[i] != nm[i]) return 2;  // name incl. NUL
    if (e.size != 5) return 3;
    if (e.type != '0') return 4;
    if (off != 1024) return 5;                           // next header at 512 + 512
    if (tar_next(arc, 1024, &off, &e)) return 6;         // past the end -> 0

    uint8_t bad[512];                                    // no ustar magic -> 0
    for (int i = 0; i < 512; i++) bad[i] = 0;
    bad[0] = 'x';
    uint32_t o2 = 0;
    if (tar_next(bad, 512, &o2, &e)) return 7;

    uint32_t o3 = 0;                                     // truncated (< 512) -> 0
    if (tar_next(arc, 100, &o3, &e)) return 8;
    return 0;
}
