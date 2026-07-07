#include "libc.h"

/* head — print the first N lines of each file (or of stdin). N defaults to 10 and
 * is set with `-n N`. Reads a stream and copies bytes to stdout until it has seen
 * N newlines. Slots into pipelines: `ls / | head -n 3`. */

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

int main(int argc, char** argv) {
    int limit = 10, ai = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) { limit = atoi(argv[2]); ai = 3; }
    if (limit <= 0) limit = 10;

    if (ai >= argc) { head_fd(0, limit); return 0; }  /* no files: stdin */
    for (int i = ai; i < argc; i++) {
        long fd = open(argv[i], O_RDONLY, 0);
        if (fd < 0) { printf("head: %s: not found\n", argv[i]); continue; }
        head_fd((int)fd, limit);
        close(fd);
    }
    return 0;
}
