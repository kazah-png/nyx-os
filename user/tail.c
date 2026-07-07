#include "libc.h"

/* tail — print the last N lines of the input (default 10, set with `-n N`). Since
 * a stream can't be seeked, we keep the most recent N lines in a circular buffer
 * and print them at EOF. Works on a file or on stdin (`ls / | tail -n 3`). With
 * several files it prints the combined last N lines (a minimal v1). */

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

int main(int argc, char** argv) {
    int ai = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) { limit = atoi(argv[2]); ai = 3; }
    if (limit <= 0) limit = 10;
    if (limit > MAXKEEP) limit = MAXKEEP;

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
