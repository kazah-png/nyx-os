#include "libc.h"

/* tail — print the last N lines of the input (default 10, set GNU-style with `-n N`,
 * `-nN`, or the bare `-N` shorthand). Since a stream can't be seeked, we keep the most
 * recent N lines in a circular buffer and print them at EOF. Works on a file or on stdin
 * (`ls / | tail -n 3`). With several files it prints the combined last N lines (a minimal
 * v1). `-n0` prints nothing (matching GNU). */

#define MAXKEEP 40
#define LLEN    256

static char ring[MAXKEEP][LLEN];
static int  total;                  /* total lines seen so far */
static int  limit = 10;             /* how many trailing lines to keep */

static void keep(const char* s) {
    int slot = total % limit;       /* circular: slot holds line `total` */
    strncpy(ring[slot], s, LLEN - 1);
    ring[slot][LLEN - 1] = '\0';
    total++;
}

static void tail_fd(int fd) {
    char buf[512], line[LLEN];
    int li = 0;
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || li >= LLEN - 1) {
                line[li] = '\0'; keep(line); li = 0;
                if (c != '\n') line[li++] = c;
            } else {
                line[li++] = c;
            }
        }
    }
    if (li > 0) { line[li] = '\0'; keep(line); }
}

static void dump(void) {
    int start = (total > limit) ? total - limit : 0;   /* first line to print */
    for (int i = start; i < total; i++) {
        char* s = ring[i % limit];
        write(1, s, strlen(s));
        write(1, "\n", 1);
    }
}

/* Parse a leading GNU line-count option: `-n N`, `-nN`, or bare `-N`. Returns the index
 * of the first non-option arg; sets *have_n when a count was given. */
static int parse_count(int argc, char** argv, int* out, int* have_n) {
    int ai = 1;
    if (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        if (a[1] == 'n') {
            if (a[2]) { *out = atoi(a + 2); *have_n = 1; ai++; }                        /* -nN  */
            else if (ai + 1 < argc) { *out = atoi(argv[ai + 1]); *have_n = 1; ai += 2; } /* -n N */
        } else {
            int dig = 1;
            for (const char* p = a + 1; *p; p++) if (*p < '0' || *p > '9') { dig = 0; break; }
            if (dig) { *out = atoi(a + 1); *have_n = 1; ai++; }                          /* -N   */
        }
    }
    return ai;
}

int main(int argc, char** argv) {
    int have_n = 0;
    int ai = parse_count(argc, argv, &limit, &have_n);
    if (have_n) { if (limit < 0) limit = 0; } else limit = 10;   /* -n0 -> 0 lines; no -n -> 10 */
    if (limit > MAXKEEP) limit = MAXKEEP;
    if (limit == 0) return 0;   /* GNU tail -n0 prints nothing (also avoids total % 0) */

    if (ai >= argc) {
        tail_fd(0);                                     /* no files: stdin */
    } else {
        for (int i = ai; i < argc; i++) {
            long fd = open(argv[i], O_RDONLY, 0);
            if (fd < 0) { printf("tail: %s: not found\n", argv[i]); continue; }
            tail_fd((int)fd);
            close(fd);
        }
    }
    dump();
    return 0;
}
