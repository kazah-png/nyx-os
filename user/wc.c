#include "libc.h"

/* wc — count lines, words and bytes of each file (or of stdin when given no
 * arguments). A "word" is a maximal run of non-whitespace, so the counter is a
 * tiny state machine: a whitespace char ends the current word, any other char
 * starts one if we were not already inside one. Output matches classic wc:
 * "<lines> <words> <bytes> [name]", with a total line when several files given. */

static void count_fd(int fd, long* lines, long* words, long* bytes) {
    char buf[512];                       /* stack buffer: always resident */
    long n;
    int inword = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            (*bytes)++;
            if (c == '\n') (*lines)++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') inword = 0;
            else if (!inword) { inword = 1; (*words)++; }
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {                      /* no args: count stdin */
        long l = 0, w = 0, b = 0;
        count_fd(0, &l, &w, &b);
        printf("%ld %ld %ld\n", l, w, b);
        return 0;
    }

    long tl = 0, tw = 0, tb = 0;
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        long fd = open(argv[i], 0, 0);
        if (fd < 0) { printf("wc: %s: not found\n", argv[i]); rc = 1; continue; }
        long l = 0, w = 0, b = 0;
        count_fd((int)fd, &l, &w, &b);
        close(fd);
        printf("%ld %ld %ld %s\n", l, w, b, argv[i]);
        tl += l; tw += w; tb += b;
    }
    if (argc > 2)                        /* more than one file: print a total */
        printf("%ld %ld %ld total\n", tl, tw, tb);
    return rc;
}
