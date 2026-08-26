#include "libc.h"

/* sort — read stdin (or each file argument) fully, sort the lines, write them out.
 * Flags (combinable, e.g. -rn): -r reverse the order, -n numeric (compare by the
 * leading integer of each line, so "10" comes after "9"), -f fold case (compare
 * case-insensitively), -u unique (after sorting, drop lines whose key equals the
 * previous line's — GNU sort -u semantics). Buffers are static (.bss, eagerly mapped
 * by the ELF loader): read() copies through the kernel and cannot fault untouched
 * lazy-heap pages in, so a fresh malloc'd buffer would truncate the read. */

#define SORT_BUF   16384
#define SORT_LINES 512

static char  buf[SORT_BUF];
static char* lines[SORT_LINES];
static int   opt_r, opt_n, opt_f, opt_u;

static long read_all(int fd, char* dst, long max) {
    long total = 0, n;
    while (total < max - 1 && (n = read(fd, dst + total, max - 1 - total)) > 0)
        total += n;
    dst[total] = '\0';
    return total;
}

/* Leading-integer key for -n (sign-aware); a non-numeric line keys as 0. */
static long numkey(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    long v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

/* Three-way key comparison, ignoring -r (so it also defines equality for -u). */
static int cmp(const char* a, const char* b) {
    if (opt_n) {
        long ka = numkey(a), kb = numkey(b);
        return (ka > kb) - (ka < kb);
    }
    return opt_f ? strcasecmp(a, b) : strcmp(a, b);
}

/* Ordering predicate: should line a come strictly before line b? Honours -r. */
static int less(const char* a, const char* b) {
    int c = cmp(a, b);
    if (opt_r) c = -c;
    return c < 0;
}

int main(int argc, char** argv) {
    int ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++)
        for (char* f = argv[ai] + 1; *f; f++) {
            if (*f == 'r') opt_r = 1;
            else if (*f == 'n') opt_n = 1;
            else if (*f == 'f') opt_f = 1;
            else if (*f == 'u') opt_u = 1;
            else { printf("sort: invalid option -%c\n", *f); return 2; }
        }

    long len = 0;
    if (ai >= argc) {
        len = read_all(0, buf, sizeof(buf));
    } else {
        for (int i = ai; i < argc && len < (long)sizeof(buf) - 1; i++) {
            long fd = open(argv[i], 0, 0);
            if (fd < 0) { printf("sort: %s: not found\n", argv[i]); return 1; }
            len += read_all((int)fd, buf + len, sizeof(buf) - len);
            close((int)fd);
        }
    }
    (void)len;

    int nl = 0;
    char* s = buf;
    while (*s && nl < SORT_LINES) {          /* split into lines (destroys buf) */
        lines[nl++] = s;
        while (*s && *s != '\n') s++;
        if (*s) *s++ = '\0';
    }

    for (int i = 1; i < nl; i++) {           /* insertion sort, stable */
        char* key = lines[i];
        int j = i - 1;
        while (j >= 0 && less(key, lines[j])) { lines[j + 1] = lines[j]; j--; }
        lines[j + 1] = key;
    }

    for (int i = 0; i < nl; i++) {
        if (opt_u && i > 0 && cmp(lines[i], lines[i - 1]) == 0) continue;  // drop equal-key runs
        write(1, lines[i], strlen(lines[i]));
        write(1, "\n", 1);
    }
    return 0;
}
