#include "libc.h"

/* sort — read stdin (or each file argument) fully, sort the lines with strcmp,
 * write them out. Buffers are static (.bss, eagerly mapped by the ELF loader):
 * read() copies through the kernel and cannot fault untouched lazy-heap pages in,
 * so a fresh malloc'd buffer would truncate the read. */

#define SORT_BUF   16384
#define SORT_LINES 512

static char  buf[SORT_BUF];
static char* lines[SORT_LINES];

static long read_all(int fd, char* dst, long max) {
    long total = 0, n;
    while (total < max - 1 && (n = read(fd, dst + total, max - 1 - total)) > 0)
        total += n;
    dst[total] = '\0';
    return total;
}

int main(int argc, char** argv) {
    long len = 0;
    if (argc < 2) {
        len = read_all(0, buf, sizeof(buf));
    } else {
        for (int i = 1; i < argc && len < (long)sizeof(buf) - 1; i++) {
            long fd = open(argv[i], 0, 0);
            if (fd < 0) { printf("sort: %s: not found\n", argv[i]); return 1; }
            len += read_all((int)fd, buf + len, sizeof(buf) - len);
            close((int)fd);
        }
    }

    int nl = 0;
    char* s = buf;
    while (*s && nl < SORT_LINES) {          /* split into lines (destroys buf) */
        lines[nl++] = s;
        while (*s && *s != '\n') s++;
        if (*s) *s++ = '\0';
    }

    for (int i = 1; i < nl; i++) {           /* insertion sort, stable enough here */
        char* key = lines[i];
        int j = i - 1;
        while (j >= 0 && strcmp(lines[j], key) > 0) { lines[j + 1] = lines[j]; j--; }
        lines[j + 1] = key;
    }

    for (int i = 0; i < nl; i++) {
        write(1, lines[i], strlen(lines[i]));
        write(1, "\n", 1);
    }
    return 0;
}
