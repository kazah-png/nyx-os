#include "libc.h"

/* touch — create each argument as an empty file if it doesn't exist (open with
 * O_CREAT and close; an existing file is left untouched — no mtime here yet). */
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: touch FILE...\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        long fd = open(argv[i], O_CREAT, 0644);
        if (fd < 0) { printf("touch: cannot create '%s'\n", argv[i]); rc = 1; continue; }
        close((int)fd);
    }
    return rc;
}
