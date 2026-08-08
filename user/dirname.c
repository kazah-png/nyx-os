#include "libc.h"

/* dirname — strip the last component from a path, like dirname(1):
 *   dirname NAME...
 *   -z   end each line with NUL instead of newline
 * Trailing slashes are ignored; with no '/', the answer is "." — matching GNU
 * (dirname "/usr/lib" -> "/usr", "usr" -> ".", "/usr/" -> "/", "/" -> "/"). */

static void emit(const char* s, int zero) {
    write(1, s, (int)strlen(s));
    char t = zero ? '\0' : '\n';
    write(1, &t, 1);
}

static void do_dirname(const char* path, int zero) {
    char buf[4096];
    strncpy(buf, path, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    int n = (int)strlen(buf);
    while (n > 1 && buf[n - 1] == '/') buf[--n] = 0;      /* strip trailing slashes */

    int slash = -1;
    for (int k = 0; k < n; k++) if (buf[k] == '/') slash = k;

    if (slash < 0) { emit(".", zero); return; }           /* no directory part */
    int e = slash;
    while (e > 0 && buf[e - 1] == '/') e--;                /* drop slashes before the last component */
    if (e == 0) { emit("/", zero); return; }              /* the root */
    buf[e] = 0;
    emit(buf, zero);
}

int main(int argc, char** argv) {
    int zero = 0, i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (argv[i][1] == '-' && !argv[i][2]) { i++; break; }        /* "--" */
        for (const char* p = argv[i] + 1; *p; p++) {
            if (*p == 'z') zero = 1;
            else { printf("dirname: invalid option -- '%c'\n", *p); return 1; }
        }
    }
    if (i >= argc) { printf("dirname: missing operand\n"); return 1; }
    for (; i < argc; i++) do_dirname(argv[i], zero);
    return 0;
}
