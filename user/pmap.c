#include "libc.h"

/* pmap — report a process's mapped memory regions. Reads /proc/<pid>/maps (the
 * kernel generates it from the process page tables) and prints each region plus a
 * total. With no argument it maps itself (getpid). */

static char buf[4096];

/* Parse a lowercase-hex run, advancing *p past it. */
static unsigned long parse_hex(const char** p) {
    unsigned long v = 0;
    for (;;) {
        char c = **p; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else break;
        v = v * 16 + d; (*p)++;
    }
    return v;
}

int main(int argc, char** argv) {
    int pid = (argc >= 2) ? atoi(argv[1]) : (int)getpid();

    char path[40];
    sprintf(path, "/proc/%d/maps", pid);
    long fd = open(path, O_RDONLY, 0);
    if (fd < 0) { printf("pmap: cannot open %s\n", path); return 1; }
    long n = read((int)fd, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    close((int)fd);

    printf("%d:   memory map\n", pid);
    printf("%s", buf);

    /* Sum each region's size (end - start) and count them. */
    unsigned long total = 0;
    int regions = 0;
    const char* p = buf;
    while (*p) {
        unsigned long start = parse_hex(&p);
        if (*p != '-') { while (*p && *p != '\n') p++; if (*p) p++; continue; }
        p++;                                      /* skip '-' */
        unsigned long end = parse_hex(&p);
        if (end > start) { total += end - start; regions++; }
        while (*p && *p != '\n') p++;             /* to end of line */
        if (*p) p++;
    }
    printf(" total   %lu regions, %lu KB mapped\n", (unsigned long)regions, total / 1024);
    return 0;
}
