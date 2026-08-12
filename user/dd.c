#include "libc.h"

/* dd — convert and copy a file, block by block, like dd(1). Operands are key=value
 * (no dashes):
 *   if=FILE   read from FILE            (default: stdin)
 *   of=FILE   write to FILE             (default: stdout; truncated unless conv=notrunc)
 *   bs=N      block size in bytes       (default: 512)
 *   count=N   copy at most N input blocks
 *   skip=N    skip N input blocks first (lseek, or read-and-discard on a pipe)
 *   seek=N    seek N output blocks first
 *   conv=...  comma list: notrunc (don't truncate of=), sync (NUL-pad a short input
 *             block up to bs), noerror (continue past a read error)
 * N takes an optional multiplier suffix: c=1, w=2, b=512, k/K=1024, M=1048576.
 * On completion "<full>+<partial> records in/out" is written to stderr (no throughput
 * line — NyxOS has no wall-clock rate). */

static int sput(char* b, int o, const char* s) { while (*s) b[o++] = *s++; return o; }
static int uput(char* b, int o, long v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (n) b[o++] = t[--n];
    return o;
}
static int starts(const char* s, const char* pfx) { return strncmp(s, pfx, strlen(pfx)) == 0; }
static int fail(const char* msg) { write(2, "dd: ", 4); write(2, msg, (int)strlen(msg)); write(2, "\n", 1); return 1; }

/* Parse a decimal count with an optional binary multiplier suffix. Returns -1 on error. */
static long parse_num(const char* s) {
    long v = 0; int any = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; any = 1; }
    if (!any) return -1;
    long mult = 1;
    switch (*s) {
        case '\0':               mult = 1;       break;
        case 'c': s++;           mult = 1;       break;
        case 'w': s++;           mult = 2;       break;
        case 'b': s++;           mult = 512;     break;
        case 'k': case 'K': s++; mult = 1024;    break;
        case 'M': s++;           mult = 1048576; break;
        default: return -1;
    }
    if (*s) return -1;                  // junk after the suffix
    return v * mult;
}

int main(int argc, char** argv) {
    const char* iff = 0; const char* off = 0;
    long bs = 512, count = -1, skip = 0, seek = 0;
    int c_notrunc = 0, c_sync = 0, c_noerror = 0;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if      (starts(a, "if="))   iff = a + 3;
        else if (starts(a, "of="))   off = a + 3;
        else if (starts(a, "bs="))    { bs   = parse_num(a + 3); if (bs   <= 0) return fail("invalid bs"); }
        else if (starts(a, "count=")) { count = parse_num(a + 6); if (count < 0) return fail("invalid count"); }
        else if (starts(a, "skip="))  { skip = parse_num(a + 5); if (skip < 0) return fail("invalid skip"); }
        else if (starts(a, "seek="))  { seek = parse_num(a + 5); if (seek < 0) return fail("invalid seek"); }
        else if (starts(a, "conv=")) {
            const char* p = a + 5;
            while (*p) {
                if      (starts(p, "notrunc")) { c_notrunc = 1; p += 7; }
                else if (starts(p, "noerror")) { c_noerror = 1; p += 7; }
                else if (starts(p, "sync"))    { c_sync    = 1; p += 4; }
                else return fail("invalid conv");
                if (*p == ',') p++;
                else if (*p) return fail("invalid conv");
            }
        }
        else return fail("unknown operand");
    }

    int in = 0, out = 1;
    if (iff) { long fd = open(iff, O_RDONLY, 0); if (fd < 0) return fail("cannot open input");  in  = (int)fd; }
    if (off) { long fd = open(off, O_CREAT | (c_notrunc ? 0 : O_TRUNC), 0644); if (fd < 0) return fail("cannot open output"); out = (int)fd; }

    unsigned char* buf = (unsigned char*)malloc((size_t)bs);
    if (!buf) return fail("cannot allocate block buffer");

    if (skip > 0) {
        if (lseek(in, skip * bs, SEEK_SET) < 0) {           // not seekable (pipe): read past
            long left = skip * bs;
            while (left > 0) { long n = read(in, buf, left < bs ? left : bs); if (n <= 0) break; left -= n; }
        }
    }
    if (seek > 0) lseek(out, seek * bs, SEEK_SET);

    long full_in = 0, part_in = 0, full_out = 0, part_out = 0;
    int status = 0;
    for (;;) {
        if (count >= 0 && (full_in + part_in) >= count) break;
        long n = read(in, buf, bs);
        if (n < 0) {
            if (c_noerror) continue;
            status = fail("read error"); break;
        }
        if (n == 0) break;                                  // EOF
        if (n == bs) full_in++;
        else { part_in++; if (c_sync) { for (long i = n; i < bs; i++) buf[i] = 0; n = bs; } }
        long w = write(out, buf, n);
        if (w < 0) { status = fail("write error"); break; }
        if (w == bs) full_out++; else part_out++;
    }

    free(buf);
    char m[96]; int o = 0;
    o = uput(m, o, full_in); m[o++] = '+'; o = uput(m, o, part_in);  o = sput(m, o, " records in\n");
    o = uput(m, o, full_out); m[o++] = '+'; o = uput(m, o, part_out); o = sput(m, o, " records out\n");
    write(2, m, o);
    return status;
}
