#include "libc.h"

/* test / [ — evaluate a conditional expression and exit 0 (true) or 1 (false):
 * the POSIX primitive behind shell conditionals, e.g. `test 3 -lt 5 && echo yes`
 * (sh already has && / || / ;). Exit status 2 is a usage/syntax error, distinct
 * from a plain false. Supported:
 *   test STR                     true if STR is non-empty
 *   test -z STR / -n STR         STR has zero / non-zero length
 *   test -e/-f/-d FILE           FILE exists / is a regular file / is a directory
 *   test ! EXPR                  negate a 1- or 2-operand EXPR
 *   test S1 = S2  /  S1 != S2    string (in)equality
 *   test N1 <op> N2              integer -eq/-ne/-lt/-le/-gt/-ge
 * The `[` spelling requires a closing `]` as the final argument. */

static int is_int(const char* s, long* out) {
    if (!s || !*s) return 0;
    int i = 0, neg = 0;
    if (s[0] == '-' || s[0] == '+') { neg = (s[0] == '-'); i = 1; if (!s[i]) return 0; }
    long v = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        v = v * 10 + (s[i] - '0');
    }
    *out = neg ? -v : v;
    return 1;
}

static int file_kind(const char* p, int* isdir, int* isreg) {
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    if (isdir) *isdir = S_ISDIR(st.st_mode);
    if (isreg) *isreg = S_ISREG(st.st_mode);
    return 1;
}

/* Evaluate an n-operand expression at a[]. Returns 1 (true), 0 (false), -1 (error). */
static int eval(char** a, int n) {
    if (n == 1) return a[0][0] != '\0';                 /* non-empty string */
    if (n == 2) {
        if (!strcmp(a[0], "!")) { int r = eval(a + 1, 1); return r < 0 ? r : !r; }
        if (!strcmp(a[0], "-z")) return a[1][0] == '\0';
        if (!strcmp(a[0], "-n")) return a[1][0] != '\0';
        int isdir = 0, isreg = 0, ex = file_kind(a[1], &isdir, &isreg);
        if (!strcmp(a[0], "-e")) return ex;
        if (!strcmp(a[0], "-f")) return ex && isreg;
        if (!strcmp(a[0], "-d")) return ex && isdir;
        return -1;
    }
    if (n == 3) {
        if (!strcmp(a[0], "!")) { int r = eval(a + 1, 2); return r < 0 ? r : !r; }
        const char* op = a[1];
        if (!strcmp(op, "=") || !strcmp(op, "==")) return strcmp(a[0], a[2]) == 0;
        if (!strcmp(op, "!="))                     return strcmp(a[0], a[2]) != 0;
        long x, y;
        if (!strcmp(op, "-eq") || !strcmp(op, "-ne") || !strcmp(op, "-lt") ||
            !strcmp(op, "-le") || !strcmp(op, "-gt") || !strcmp(op, "-ge")) {
            if (!is_int(a[0], &x) || !is_int(a[2], &y)) return -1;
            if (!strcmp(op, "-eq")) return x == y;
            if (!strcmp(op, "-ne")) return x != y;
            if (!strcmp(op, "-lt")) return x <  y;
            if (!strcmp(op, "-le")) return x <= y;
            if (!strcmp(op, "-gt")) return x >  y;
            return x >= y;                               /* -ge */
        }
        return -1;
    }
    if (n == 4) {                                       /* only `! <3-operand expr>` */
        if (!strcmp(a[0], "!")) { int r = eval(a + 1, 3); return r < 0 ? r : !r; }
        return -1;
    }
    return -1;
}

int main(int argc, char** argv) {
    /* When invoked as `[`, the last argument must be a literal `]` (then dropped). */
    const char* base = argv[0];
    for (const char* p = argv[0]; *p; p++) if (*p == '/') base = p + 1;
    if (!strcmp(base, "[")) {
        if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
            printf("[: missing ']'\n");
            return 2;
        }
        argc--;                                         /* hide the trailing ] */
    }
    int n = argc - 1;
    if (n == 0) return 1;                               /* no expression: false */
    int r = eval(argv + 1, n);
    if (r < 0) { printf("test: invalid expression\n"); return 2; }
    return r ? 0 : 1;
}
