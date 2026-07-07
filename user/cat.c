#include "libc.h"

/* cat — concatenate files (or stdin) to stdout. With no arguments it copies
 * fd 0 -> fd 1, so `echo hi | cat` works and inside a pipeline it is a
 * pass-through. With file arguments it open()s each in turn and streams it out;
 * a missing file is reported and skipped (exit status 1), the rest still run. */

static void cat_fd(int fd) {
    char buf[512];                       /* stack buffer: always resident */
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, n);
}

int main(int argc, char** argv) {
    if (argc < 2) { cat_fd(0); return 0; }   /* no args: stdin -> stdout */

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        long fd = open(argv[i], 0, 0);
        if (fd < 0) { printf("cat: %s: not found\n", argv[i]); rc = 1; continue; }
        cat_fd((int)fd);
        close(fd);
    }
    return rc;
}
