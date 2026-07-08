#include "libc.h"

/* rm — unlink each argument (file, or directory via the VFS's unlink). */
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: rm PATH...\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) != 0) {
            printf("rm: cannot remove '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
