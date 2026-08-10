#include "libc.h"

/* cmp — compare two files byte by byte, like cmp(1). With no options it prints where the
 * files first differ ("FILE1 FILE2 differ: byte N, line M") and exits 1; identical files
 * produce no output and exit 0; a read error / missing file exits 2. If one file is a
 * prefix of the other, "cmp: EOF on <shorter> after byte N, in line M" goes to stderr.
 *   -s   silent: print nothing, just set the exit status
 * Either FILE may be "-" for standard input. (-l/-b are not implemented — the -l column
 * width is derived from the file size, which streaming compare doesn't know up front.) */

typedef struct { int fd; unsigned char buf[4096]; int len, pos, eof; } reader_t;

static int rinit(reader_t* r, const char* p) {
    if (p[0] == '-' && p[1] == '\0') r->fd = 0;
    else { long f = open(p, O_RDONLY, 0); if (f < 0) return -1; r->fd = (int)f; }
    r->len = r->pos = r->eof = 0;
    return 0;
}
static int rbyte(reader_t* r) {
    if (r->pos >= r->len) {
        if (r->eof) return -1;
        r->len = (int)read(r->fd, r->buf, sizeof(r->buf));
        r->pos = 0;
        if (r->len <= 0) { r->eof = 1; return -1; }
    }
    return r->buf[r->pos++];
}

/* tiny formatters so the exact output bytes are ours (no printf %-specifier guessing) */
static int sput(char* b, int o, const char* s) { while (*s) b[o++] = *s++; return o; }
static int uput(char* b, int o, unsigned long v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (n) b[o++] = t[--n];
    return o;
}

int main(int argc, char** argv) {
    int silent = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == '-' && a[2] == '\0') { ai++; break; }        // "--" ends options
        int ok = 1;
        for (int k = 1; a[k]; k++) { if (a[k] == 's') silent = 1; else { ok = 0; break; } }
        if (!ok) {
            char m[128]; int o = sput(m, 0, "cmp: invalid option "); o = sput(m, o, a); m[o++] = '\n';
            write(2, m, o); return 2;
        }
    }
    if (argc - ai < 2) { const char* u = "cmp: usage: cmp [-s] FILE1 FILE2\n"; write(2, u, (int)strlen(u)); return 2; }
    const char* f1 = argv[ai], * f2 = argv[ai + 1];

    reader_t r1, r2;
    if (rinit(&r1, f1) < 0) {
        if (!silent) { char m[300]; int o = sput(m, 0, "cmp: "); o = sput(m, o, f1); o = sput(m, o, ": No such file or directory\n"); write(2, m, o); }
        return 2;
    }
    if (rinit(&r2, f2) < 0) {
        if (!silent) { char m[300]; int o = sput(m, 0, "cmp: "); o = sput(m, o, f2); o = sput(m, o, ": No such file or directory\n"); write(2, m, o); }
        return 2;
    }

    unsigned long pos = 0, line = 1;
    for (;;) {
        int c1 = rbyte(&r1), c2 = rbyte(&r2);
        if (c1 < 0 && c2 < 0) return 0;                          // both EOF together: identical
        if (c1 < 0 || c2 < 0) {                                  // one is a prefix of the other
            if (!silent) {
                const char* sh = c1 < 0 ? f1 : f2;
                char m[320]; int o = sput(m, 0, "cmp: EOF on '"); o = sput(m, o, sh); o = sput(m, o, "'");
                if (pos == 0) o = sput(m, o, " which is empty");   // GNU special-cases an empty file
                else { o = sput(m, o, " after byte "); o = uput(m, o, pos); o = sput(m, o, ", in line "); o = uput(m, o, line); }
                m[o++] = '\n';
                write(2, m, o);
            }
            return 1;
        }
        pos++;
        if (c1 != c2) {                                          // first difference
            if (!silent) {
                char m[320]; int o = sput(m, 0, f1); m[o++] = ' '; o = sput(m, o, f2);
                o = sput(m, o, " differ: char "); o = uput(m, o, pos);   // "char" (POSIX / C locale)
                o = sput(m, o, ", line "); o = uput(m, o, line); m[o++] = '\n';
                write(1, m, o);
            }
            return 1;
        }
        if (c1 == '\n') line++;                                  // newline advances the line for the next byte
    }
}
