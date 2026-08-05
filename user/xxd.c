#include "libc.h"

/* xxd — a canonical hex + ASCII dump of a file (or stdin), matching xxd(1)'s
 * default layout: an 8-hex-digit byte offset, then 16 bytes shown as eight
 * space-separated 2-byte groups, then a printable-ASCII gutter (bytes outside
 * 0x20..0x7e shown as '.'). Options:
 *   -l LEN   stop after LEN bytes (decimal, or 0x.. hex)
 *   -s OFF   skip OFF bytes first, and start the offset column there
 *   -c COLS  bytes per line (default 16, 1..64)
 * Unlike the kernel's `hexdump <addr>` builtin — which inspects live MEMORY — this
 * reads FILE contents, so you can eyeball any binary on disk (an ELF, a PNG, the
 * initramfs). Reads a file argument, or standard input when given none, so it also
 * works mid-pipeline (`cat foo | xxd`). */

static const char HEX[] = "0123456789abcdef";

/* One-byte-at-a-time reader over a refilled block buffer (like nl/cat), so the
 * dump loop can honour -l/-s without juggling partial reads. */
static unsigned char rbuf[512];
static int rlen = 0, rpos = 0;
static int get_byte(int fd) {
    if (rpos >= rlen) {
        rlen = (int)read(fd, rbuf, sizeof(rbuf));
        rpos = 0;
        if (rlen <= 0) return -1;
    }
    return rbuf[rpos++];
}

/* Emit one dump line: offset ": " hex-field (padded so short lines still align)
 * two spaces, then the ASCII gutter for the bytes actually present. */
static void emit(unsigned long off, const unsigned char* line, int have, int cols) {
    char out[400];
    int o = 0;
    for (int sh = 28; sh >= 0; sh -= 4) out[o++] = HEX[(off >> sh) & 0xF];
    out[o++] = ':'; out[o++] = ' ';
    for (int i = 0; i < cols; i++) {
        if (i > 0 && (i & 1) == 0) out[o++] = ' ';   /* space before each 2-byte group */
        if (i < have) { out[o++] = HEX[line[i] >> 4]; out[o++] = HEX[line[i] & 0xF]; }
        else          { out[o++] = ' ';              out[o++] = ' '; }
    }
    out[o++] = ' '; out[o++] = ' ';                  /* fixed 2-space gutter separator */
    for (int i = 0; i < have; i++) {
        unsigned char c = line[i];
        out[o++] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
    }
    out[o++] = '\n';
    write(1, out, o);
}

int main(int argc, char** argv) {
    long limit = -1;                 /* -l: byte cap, -1 = whole input */
    long skip  = 0;                  /* -s: leading bytes to discard */
    int  cols  = 16;                 /* -c: bytes per line */

    int ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        const char* v = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0);
        if      (a[1] == 'l' && v) limit = strtol(v, 0, 0);
        else if (a[1] == 's' && v) { skip = strtol(v, 0, 0); if (skip < 0) skip = 0; }
        else if (a[1] == 'c' && v) { cols = atoi(v); if (cols < 1) cols = 1; if (cols > 64) cols = 64; }
        else { printf("xxd: invalid option %s\n", a); return 1; }
    }

    int fd = 0;                      /* stdin by default */
    if (ai < argc) {
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("xxd: %s: not found\n", argv[ai]); return 1; }
        fd = (int)f;
    }

    for (long s = skip; s > 0; s--) if (get_byte(fd) < 0) break;   /* discard -s bytes */

    unsigned char line[64];
    int have = 0;
    long total = 0;
    unsigned long off = (unsigned long)skip;                      /* offsets start at the seek point */
    while (limit < 0 || total < limit) {
        int b = get_byte(fd);
        if (b < 0) break;
        line[have++] = (unsigned char)b;
        total++;
        if (have == cols) { emit(off, line, have, cols); off += (unsigned)cols; have = 0; }
    }
    if (have > 0) emit(off, line, have, cols);

    if (fd != 0) close(fd);
    return 0;
}
