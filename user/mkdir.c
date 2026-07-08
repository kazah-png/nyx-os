#include "libc.h"

/* mkdir — create each directory argument (cwd-relative or absolute). */
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: mkdir DIR...\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0755) != 0) {
            printf("mkdir: cannot create '%s'\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
