#include "libc.h"

/* head — print the first part of each file (or of stdin). By default the first 10
 * LINES; `-n N` (also `-nN` or the bare `-N`) sets the line count, and `-c N` (or
 * `-cN`) prints the first N BYTES instead. Streams, so it slots into pipelines:
 * `ls / | head -n 3`, `head -c 16 file`. `-n0`/`-c0` print nothing (matching GNU). */

static void head_lines(int fd, int limit) {
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

static void head_bytes(int fd, long limit) {
    char buf[512];
    long n, done = 0;
    while (done < limit && (n = read(fd, buf, sizeof(buf))) > 0) {
        long take = (n < limit - done) ? n : (limit - done);
        write(1, buf, take);
        done += take;
    }
}

/* Parse a leading option: `-n N`/`-nN`/`-N` (lines) or `-c N`/`-cN` (bytes). Sets *limit,
 * *have and *bytes, and returns the index of the first non-option arg. */
static int parse_opts(int argc, char** argv, int* limit, int* have, int* bytes) {
    int ai = 1;
    if (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        if (a[1] == 'n' || a[1] == 'c') {
            *bytes = (a[1] == 'c');
            if (a[2]) { *limit = atoi(a + 2); *have = 1; ai++; }                          /* -nN/-cN */
            else if (ai + 1 < argc) { *limit = atoi(argv[ai + 1]); *have = 1; ai += 2; }  /* -n N/-c N */
            else ai++;
        } else {
            int dig = 1;
            for (const char* p = a + 1; *p; p++) if (*p < '0' || *p > '9') { dig = 0; break; }
            if (dig) { *limit = atoi(a + 1); *have = 1; ai++; }                           /* -N */
        }
    }
    return ai;
}

static void head_fd(int fd, int limit, int bytes) {
    if (bytes) head_bytes(fd, limit);
    else       head_lines(fd, limit);
}

int main(int argc, char** argv) {
    int limit = 10, have = 0, bytes = 0;
    int ai = parse_opts(argc, argv, &limit, &have, &bytes);
    if (have) { if (limit < 0) limit = 0; } else limit = 10;   /* -0 -> nothing; no flag -> 10 lines */

    if (ai >= argc) { head_fd(0, limit, bytes); return 0; }    /* no files: stdin */
    for (int i = ai; i < argc; i++) {
        long fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) { printf("head: %s: not found\n", argv[i]); continue; }
        head_fd((int)fd, limit, bytes);
        close(fd);
    }
    return 0;
}
