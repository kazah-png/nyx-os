#include "libc.h"

/* head — print the first N lines of each file (or of stdin). N defaults to 10 and
 * is set GNU-style with `-n N`, `-nN`, or the bare `-N` shorthand (`head -3`).
 * Reads a stream and copies bytes to stdout until it has seen N newlines. Slots into
 * pipelines: `ls / | head -n 3`. `-n0` prints nothing (matching GNU). */

static void head_fd(int fd, int limit) {
    char buf[512];
    long n;
    int lines = 0;
    while (lines < limit && (n = read(fd, buf, sizeof(buf))) > 0) {
        long start = 0;
        for (long i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                lines++;
                if (lines >= limit) {                 /* flush up to and incl. this \n */
                    write(1, buf + start, (i + 1) - start);
                    return;
                }
            }
        }
        write(1, buf + start, n - start);             /* whole chunk, limit not reached */
    }
}

/* Parse a leading GNU line-count option: `-n N`, `-nN`, or bare `-N`. Sets *limit and
 * *have_n and returns the index of the first non-option arg. */
static int parse_count(int argc, char** argv, int* limit, int* have_n) {
    int ai = 1;
    if (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        if (a[1] == 'n') {
            if (a[2]) { *limit = atoi(a + 2); *have_n = 1; ai++; }                      /* -nN  */
            else if (ai + 1 < argc) { *limit = atoi(argv[ai + 1]); *have_n = 1; ai += 2; } /* -n N */
        } else {
            int dig = 1;
            for (const char* p = a + 1; *p; p++) if (*p < '0' || *p > '9') { dig = 0; break; }
            if (dig) { *limit = atoi(a + 1); *have_n = 1; ai++; }                        /* -N   */
        }
    }
    return ai;
}

int main(int argc, char** argv) {
    int limit = 10, have_n = 0;
    int ai = parse_count(argc, argv, &limit, &have_n);
    if (have_n) { if (limit < 0) limit = 0; } else limit = 10;   /* -n0 -> 0 lines; no -n -> 10 */

    if (ai >= argc) { head_fd(0, limit); return 0; }  /* no files: stdin */
    for (int i = ai; i < argc; i++) {
        long fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) { printf("head: %s: not found\n", argv[i]); continue; }
        head_fd((int)fd, limit);
        close(fd);
    }
    return 0;
}
