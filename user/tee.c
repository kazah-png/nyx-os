#include "libc.h"

// O_WRONLY isn't defined on NyxOS (its fds are read/write regardless of an access-mode
// bit, so cp.c opens with just O_CREAT|O_TRUNC); define it to 0 here so the same source
// also builds host-side (where <fcntl.h> gives O_WRONLY its real value) for the diff test.
#ifndef O_WRONLY
#define O_WRONLY 0
#endif

/* tee — copy standard input to standard output AND to each FILE, like tee(1).
 *   tee [-a] [-i] [FILE...]
 *   -a  append to the files instead of truncating them
 *   -i  ignore interrupts (accepted for compatibility; a no-op here)
 * Every byte read from stdin is written to stdout and to each named file. With no files
 * it behaves like cat. Returns 0 if all files opened, 1 if any could not (the rest, and
 * stdout, are still written). The companion to a shell pipeline's `... | tee log`. */

#define TEE_MAXF 32

int main(int argc, char** argv) {
    int append = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == '-' && !a[2]) { ai++; break; }        // "--" ends the options
        int ok = 1;
        for (int k = 1; a[k]; k++) {
            if      (a[k] == 'a') append = 1;
            else if (a[k] == 'i') { /* ignore interrupts: a no-op here */ }
            else { ok = 0; break; }
        }
        if (!ok) { printf("tee: invalid option %s\n", a); return 1; }
    }

    int fds[TEE_MAXF], nf = 0, rc = 0;
    for (; ai < argc; ai++) {
        if (nf >= TEE_MAXF) break;
        int flags = append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
        long f = open(argv[ai], flags, 0644);
        if (f < 0) { printf("tee: %s: cannot open\n", argv[ai]); rc = 1; continue; }
        fds[nf++] = (int)f;
    }

    char buf[4096];
    for (;;) {
        long n = read(0, buf, sizeof(buf));
        if (n <= 0) break;
        write(1, buf, (int)n);                            // standard output
        for (int i = 0; i < nf; i++) write(fds[i], buf, (int)n);
    }
    for (int i = 0; i < nf; i++) close(fds[i]);
    return rc;
}
