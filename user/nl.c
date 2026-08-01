#include "libc.h"

/* nl — number the lines of a file (or stdin), like GNU nl's core.
 *   nl [-b a|t] [-w WIDTH] [-s SEP] [file]
 *   -b a   number every line
 *   -b t   number only non-empty lines (the default)
 *   -w N   width of the line-number field (default 6, right-justified)
 *   -s SEP separator printed after the number (default a TAB)
 * The running count advances only on numbered lines; a skipped (blank) line is
 * printed unchanged. Reads a file argument or, with none, standard input. */

static char rbuf[1024];
static int  rlen = 0, rpos = 0;

static int refill(int fd) { rpos = 0; rlen = (int)read(fd, rbuf, sizeof(rbuf)); return rlen; }

static int read_line(int fd, char* line, int cap) {
    int len = 0;
    for (;;) {
        if (rpos >= rlen) { if (refill(fd) <= 0) break; }
        char ch = rbuf[rpos++];
        if (ch == '\n') return len;
        if (len < cap - 1) line[len++] = ch;
    }
    return len > 0 ? len : -1;
}

int main(int argc, char** argv) {
    int body_all = 0, width = 6, ai = 1;
    const char* sep = "\t";

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == 'b') {
            const char* v = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0);
            if (v) { if (v[0] == 'a') body_all = 1; else if (v[0] == 't') body_all = 0; }
        } else if (a[1] == 'w') {
            const char* v = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0);
            if (v) { width = atoi(v); if (width < 1) width = 1; if (width > 20) width = 20; }
        } else if (a[1] == 's') {
            const char* v = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0);
            if (v) sep = v;
        } else { printf("nl: invalid option %s\n", a); return 1; }
    }

    int fd = 0;   /* stdin by default */
    if (ai < argc) {
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("nl: %s: not found\n", argv[ai]); return 1; }
        fd = (int)f;
    }

    char line[1024], out[1152];
    long count = 1;
    for (;;) {
        int len = read_line(fd, line, sizeof(line));
        if (len < 0) break;

        if (body_all || len > 0) {                 /* a numbered line */
            char digs[24];
            int dn = 0;
            long v = count;
            if (v == 0) digs[dn++] = '0';
            while (v > 0) { digs[dn++] = (char)('0' + (v % 10)); v /= 10; }

            int oi = 0, pad = width - dn;
            for (int i = 0; i < pad; i++) out[oi++] = ' ';       /* right-justify */
            while (dn > 0) out[oi++] = digs[--dn];
            for (int i = 0; sep[i] && oi < (int)sizeof(out) - 1; i++) out[oi++] = sep[i];
            for (int i = 0; i < len && oi < (int)sizeof(out) - 1; i++) out[oi++] = line[i];
            write(1, out, oi);
            write(1, "\n", 1);
            count++;
        } else {                                   /* skipped blank line */
            write(1, line, len);
            write(1, "\n", 1);
        }
    }

    if (fd != 0) close(fd);
    return 0;
}
