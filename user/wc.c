#include "libc.h"

/* wc — count lines, words and bytes of each file (or of stdin when given no
 * file arguments). -l/-w/-c (or -m) select which of the counts to print, and -L
 * the length of the longest line, in that fixed order; with no flag l/w/c are
 * shown (classic wc). A "word" is a maximal run of non-whitespace. -L counts a
 * line's display width (tabs advance to the next multiple of 8, the newline is
 * not counted), matching GNU. With several files a "total" line follows; its -L
 * is the MAX line length across the files, not a sum. */

static void count_fd(int fd, long* lines, long* words, long* bytes, long* maxlen) {
    char buf[512];
    long n;
    int inword = 0, cur = 0, mx = 0;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            (*bytes)++;
            if (c == '\n')      { (*lines)++; if (cur > mx) mx = cur; cur = 0; }
            else if (c == '\t') cur += 8 - (cur % 8);
            else                cur++;
            if (c == ' ' || c == '\n' || c == '\t' || c == '\r') inword = 0;
            else if (!inword) { inword = 1; (*words)++; }
        }
    }
    if (cur > mx) mx = cur;                       /* final line with no trailing newline */
    *maxlen = mx;
}

static void emit(int wl, int ww, int wc, int wL, long l, long w, long b, long L, const char* name) {
    int first = 1;
    if (wl) { printf(first ? "%ld" : " %ld", l); first = 0; }
    if (ww) { printf(first ? "%ld" : " %ld", w); first = 0; }
    if (wc) { printf(first ? "%ld" : " %ld", b); first = 0; }
    if (wL) { printf(first ? "%ld" : " %ld", L); first = 0; }
    if (name) printf(" %s", name);
    printf("\n");
}

int main(int argc, char** argv) {
    int wl = 0, ww = 0, wc = 0, wL = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        for (const char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'l') wl = 1;
            else if (*f == 'w') ww = 1;
            else if (*f == 'c' || *f == 'm') wc = 1;
            else if (*f == 'L') wL = 1;
            else { printf("wc: invalid option -%c\n", *f); return 1; }
        }
    }
    if (!wl && !ww && !wc && !wL) { wl = ww = wc = 1; }   /* default: l w c */

    if (ai >= argc) {                                 /* no files: count stdin */
        long l = 0, w = 0, b = 0, L = 0;
        count_fd(0, &l, &w, &b, &L);
        emit(wl, ww, wc, wL, l, w, b, L, 0);
        return 0;
    }

    long tl = 0, tw = 0, tb = 0, tL = 0;
    int rc = 0, nfiles = 0;
    for (int i = ai; i < argc; i++) {
        long fd = open(argv[i], 0, 0);
        if (fd < 0) { printf("wc: %s: not found\n", argv[i]); rc = 1; continue; }
        long l = 0, w = 0, b = 0, L = 0;
        count_fd((int)fd, &l, &w, &b, &L);
        close(fd);
        emit(wl, ww, wc, wL, l, w, b, L, argv[i]);
        tl += l; tw += w; tb += b; if (L > tL) tL = L; nfiles++;
    }
    if (nfiles > 1) emit(wl, ww, wc, wL, tl, tw, tb, tL, "total");
    return rc;
}
