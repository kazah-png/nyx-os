#include "libc.h"

/* wc — count lines, words and bytes of each file (or of stdin when given no
 * file arguments). -l/-w/-c (or -m) select which of the counts to print, in
 * that fixed order; with no flag all three are shown (classic wc). A "word" is
 * a maximal run of non-whitespace. With several files a "total" line follows. */

static void count_fd(int fd, long* lines, long* words, long* bytes) {
    char buf[512];
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

static void emit(int wl, int ww, int wc, long l, long w, long b, const char* name) {
    int first = 1;
    if (wl) { printf(first ? "%ld" : " %ld", l); first = 0; }
    if (ww) { printf(first ? "%ld" : " %ld", w); first = 0; }
    if (wc) { printf(first ? "%ld" : " %ld", b); first = 0; }
    if (name) printf(" %s", name);
    printf("\n");
}

int main(int argc, char** argv) {
    int wl = 0, ww = 0, wc = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        for (const char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'l') wl = 1;
            else if (*f == 'w') ww = 1;
            else if (*f == 'c' || *f == 'm') wc = 1;
            else { printf("wc: invalid option -%c\n", *f); return 1; }
        }
    }
    if (!wl && !ww && !wc) { wl = ww = wc = 1; }      /* default: all three */

    if (ai >= argc) {                                 /* no files: count stdin */
        long l = 0, w = 0, b = 0;
        count_fd(0, &l, &w, &b);
        emit(wl, ww, wc, l, w, b, 0);
        return 0;
    }

    long tl = 0, tw = 0, tb = 0;
    int rc = 0, nfiles = 0;
    for (int i = ai; i < argc; i++) {
        long fd = open(argv[i], 0, 0);
        if (fd < 0) { printf("wc: %s: not found\n", argv[i]); rc = 1; continue; }
        long l = 0, w = 0, b = 0;
        count_fd((int)fd, &l, &w, &b);
        close(fd);
        emit(wl, ww, wc, l, w, b, argv[i]);
        tl += l; tw += w; tb += b; nfiles++;
    }
    if (nfiles > 1) emit(wl, ww, wc, tl, tw, tb, "total");
    return rc;
}
