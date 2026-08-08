#include "libc.h"

/* basename — strip directory and suffix from a path, like basename(1):
 *   basename NAME [SUFFIX]
 *   basename -a NAME...            (every argument is a NAME)
 *   basename -s SUFFIX NAME...     (remove SUFFIX from each; implies -a)
 *   -z   end each line with NUL instead of newline
 * Trailing slashes are stripped; a path that is all slashes prints "/". A SUFFIX
 * is removed only when it is a proper suffix (never the whole component). */

static void emit(const char* s, int zero) {
    write(1, s, (int)strlen(s));
    char t = zero ? '\0' : '\n';
    write(1, &t, 1);
}

static void do_basename(const char* name, const char* suffix, int zero) {
    char buf[4096];
    strncpy(buf, name, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    int n = (int)strlen(buf);
    while (n > 1 && buf[n - 1] == '/') buf[--n] = 0;      /* strip trailing slashes */

    const char* base;
    if (n == 1 && buf[0] == '/') base = "/";              /* path was all slashes */
    else { char* s = strrchr(buf, '/'); base = s ? s + 1 : buf; }

    char out[4096];
    strcpy(out, base);
    if (suffix && *suffix) {                               /* strip a proper suffix */
        int bl = (int)strlen(out), sl = (int)strlen(suffix);
        if (bl > sl && strcmp(out + bl - sl, suffix) == 0) out[bl - sl] = 0;
    }
    emit(out, zero);
}

int main(int argc, char** argv) {
    int zero = 0, multiple = 0; const char* suffix = 0; int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (argv[i][1] == '-' && !argv[i][2]) { i++; break; }        /* "--" */
        for (const char* p = argv[i] + 1; *p; p++) {
            if (*p == 'z') zero = 1;
            else if (*p == 'a') multiple = 1;
            else if (*p == 's') {
                const char* v = p[1] ? p + 1 : (i + 1 < argc ? argv[++i] : 0);
                if (!v) { printf("basename: option requires an argument -- 's'\n"); return 1; }
                suffix = v; multiple = 1; break;
            } else { printf("basename: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    int rest = argc - i;
    if (rest < 1) { printf("basename: missing operand\n"); return 1; }
    if (multiple || suffix) {
        for (; i < argc; i++) do_basename(argv[i], suffix, zero);
    } else {
        if (rest > 2) { printf("basename: extra operand '%s'\n", argv[i + 2]); return 1; }
        do_basename(argv[i], rest >= 2 ? argv[i + 1] : 0, zero);
    }
    return 0;
}
