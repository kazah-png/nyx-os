#include "libc.h"

/* uniq — filter adjacent matching lines (like GNU uniq's core). Reads a file
 * argument or, with none, standard input, and collapses each run of identical
 * adjacent lines into one. Pairs with `sort`: `sort f | uniq`.
 *   uniq [-c] [-d] [-u] [file]
 *   -c  prefix each output line with the number of times it occurred
 *   -d  only print lines that were repeated (a run longer than one)
 *   -u  only print lines that were unique (a run of exactly one)
 * Note: like real uniq, only ADJACENT duplicates are collapsed, so input is
 * usually sorted first. */

/* A small buffered line reader over one fd (files here are modest). */
static char rbuf[1024];
static int  rlen = 0, rpos = 0;

static int refill(int fd) {
    rpos = 0;
    rlen = (int)read(fd, rbuf, sizeof(rbuf));
    return rlen;
}

/* Read one line (without the trailing '\n') into `line`; returns its length,
 * or -1 at end of input. A final line without a newline is still returned. */
static int read_line(int fd, char* line, int cap) {
    int len = 0;
    for (;;) {
        if (rpos >= rlen) {
            if (refill(fd) <= 0) break;
        }
        char ch = rbuf[rpos++];
        if (ch == '\n') return len;
        if (len < cap - 1) line[len++] = ch;   /* overlong lines are truncated */
    }
    return len > 0 ? len : -1;
}

static void emit(const char* line, int count, int cflag, int dflag, int uflag) {
    if (dflag && count < 2) return;   /* -d: only repeated runs */
    if (uflag && count > 1) return;   /* -u: only single occurrences */
    if (cflag) printf("%d %s\n", count, line);
    else       printf("%s\n", line);
}

int main(int argc, char** argv) {
    int cflag = 0, dflag = 0, uflag = 0, ai = 1;

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        for (const char* f = argv[ai] + 1; *f; f++) {
            if      (*f == 'c') cflag = 1;
            else if (*f == 'd') dflag = 1;
            else if (*f == 'u') uflag = 1;
            else { printf("uniq: invalid option -%c\n", *f); return 1; }
        }
    }

    int fd = 0;   /* stdin by default */
    if (ai < argc) {
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("uniq: %s: not found\n", argv[ai]); return 1; }
        fd = (int)f;
    }

    char prev[1024], cur[1024];
    int have_prev = 0, count = 0;
    for (;;) {
        int len = read_line(fd, cur, sizeof(cur));
        if (len < 0) break;
        cur[len] = '\0';
        if (have_prev && strcmp(cur, prev) == 0) { count++; continue; }
        if (have_prev) emit(prev, count, cflag, dflag, uflag);
        memcpy(prev, cur, (unsigned)len + 1);
        have_prev = 1;
        count = 1;
    }
    if (have_prev) emit(prev, count, cflag, dflag, uflag);

    if (fd != 0) close(fd);
    return 0;
}
