#include "libc.h"

/* rev — reverse the byte order of every line, like rev(1):
 *   rev [FILE...]
 * Each line is emitted with its characters in reverse order; only '\n' delimits a
 * line, and the delimiter stays put (a final line with no trailing newline is
 * reversed and printed without one). With no FILE (or "-") it reverses standard
 * input. Byte-oriented (the C locale), the same reversal util-linux's rev does.
 * Lines are buffered in a fixed 64 KiB line buffer (ample for text). */

static char line[65536];
static int  nl;                 /* bytes currently buffered in `line` */

static void emit_reversed(int add_nl) {
    for (int a = 0, b = nl - 1; a < b; a++, b--) {   /* reverse in place */
        char t = line[a]; line[a] = line[b]; line[b] = t;
    }
    if (nl) write(1, line, nl);
    if (add_nl) write(1, "\n", 1);
    nl = 0;
}

static void rev_fd(int fd) {
    char buf[8192]; int n;
    nl = 0;
    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') emit_reversed(1);
            else if (nl < (int)sizeof(line)) line[nl++] = buf[i];
            /* bytes past 64 KiB on one line are dropped (no realistic text hits this) */
        }
    }
    if (nl) emit_reversed(0);    /* trailing line with no newline */
}

int main(int argc, char** argv) {
    int i = 1;
    if (i >= argc) { rev_fd(0); return 0; }
    int rc = 0;
    for (; i < argc; i++) {
        if (argv[i][0] == '-' && !argv[i][1]) { rev_fd(0); continue; }
        long f = open(argv[i], O_RDONLY, 0);
        if (f < 0) { printf("rev: cannot open %s\n", argv[i]); rc = 1; continue; }
        rev_fd((int)f);
        close((int)f);
    }
    return rc;
}
