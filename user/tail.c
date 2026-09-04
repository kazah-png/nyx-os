#include "libc.h"

/* tail — print the last N lines of the input (default 10, set GNU-style with `-n N`,
 * `-nN`, or the bare `-N` shorthand), OR the last N BYTES with `-c N` / `-cN` (like
 * `head -c`). Since a stream can't be seeked, we keep the most recent N lines (or bytes)
 * in a circular buffer and print them at EOF. Works on a file or on stdin (`ls / | tail
 * -n 3`, `tail -c 20 file`). With several files it prints the combined tail (a minimal
 * v1). `-n0`/`-c0` print nothing (matching GNU). Byte mode caps at MAXBYTES. */

#define MAXKEEP 40
#define LLEN    256
#define MAXBYTES 8192

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

/* -c byte mode: keep the last `climit` bytes in a ring, print them in order at EOF. */
static char bring[MAXBYTES];
static long bcount;                  /* total bytes seen */
static int  climit;                  /* how many trailing bytes to keep (>0 in byte mode) */

static void tail_c_fd(int fd) {
    char buf[512];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        for (long i = 0; i < n; i++) bring[bcount++ % climit] = buf[i];
}

static void dump_c(void) {
    if (bcount <= climit) { write(1, bring, bcount); return; }
    long sp = bcount % climit;             /* oldest retained byte is at sp */
    write(1, bring + sp, climit - sp);     /* sp..end (older half) */
    write(1, bring, sp);                   /* 0..sp   (newer, wrapped half) */
}

/* Parse a leading count option: lines `-n N`/`-nN`/bare `-N`, or bytes `-c N`/`-cN`.
 * Returns the index of the first non-option arg; sets *have when a count was given and
 * *bytes when it was `-c` (byte mode). */
static int parse_count(int argc, char** argv, int* out, int* have, int* bytes) {
    int ai = 1;
    if (ai < argc && argv[ai][0] == '-' && argv[ai][1]) {
        const char* a = argv[ai];
        if (a[1] == 'n' || a[1] == 'c') {
            *bytes = (a[1] == 'c');
            if (a[2]) { *out = atoi(a + 2); *have = 1; ai++; }                          /* -nN / -cN */
            else if (ai + 1 < argc) { *out = atoi(argv[ai + 1]); *have = 1; ai += 2; }  /* -n N / -c N */
        } else {
            int dig = 1;
            for (const char* p = a + 1; *p; p++) if (*p < '0' || *p > '9') { dig = 0; break; }
            if (dig) { *out = atoi(a + 1); *have = 1; ai++; }                           /* -N (lines) */
        }
    }
    return ai;
}

int main(int argc, char** argv) {
    int have = 0, bytes = 0, count = 10;
    int ai = parse_count(argc, argv, &count, &have, &bytes);

    if (bytes) {                                     /* -c: last N bytes */
        climit = have ? count : 10;
        if (climit < 0) climit = 0;
        if (climit > MAXBYTES) climit = MAXBYTES;
        if (climit == 0) return 0;                   /* -c0 prints nothing (avoids % 0) */
    } else {                                         /* -n / -N: last N lines */
        limit = have ? count : 10;
        if (limit < 0) limit = 0;
        if (limit > MAXKEEP) limit = MAXKEEP;
        if (limit == 0) return 0;                    /* -n0 prints nothing (avoids % 0) */
    }

    if (ai >= argc) {
        if (bytes) tail_c_fd(0); else tail_fd(0);    /* no files: stdin */
    } else {
        for (int i = ai; i < argc; i++) {
            long fd = open(argv[i], O_RDONLY, 0);
            if (fd < 0) { printf("tail: %s: not found\n", argv[i]); continue; }
            if (bytes) tail_c_fd((int)fd); else tail_fd((int)fd);
            close(fd);
        }
    }
    if (bytes) dump_c(); else dump();
    return 0;
}
