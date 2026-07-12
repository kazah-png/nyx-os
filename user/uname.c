#include "libc.h"

/* uname — print system information. With no args prints just the OS name; with any
 * argument (e.g. -a) prints the full kernel banner from /proc/version, which reads
 * like "NyxOS version 5.8.35 (x86_64)". */

static char buf[256];

int main(int argc, char** argv) {
    if (argc < 2) { printf("NyxOS\n"); return 0; }
    (void)argv;
    long fd = open("/proc/version", O_RDONLY, 0);
    if (fd < 0) { printf("NyxOS\n"); return 0; }
    long n = read((int)fd, buf, sizeof(buf) - 1);
    if (n < 0) n = 0;
    buf[n] = '\0';
    close((int)fd);
    printf("%s", buf);
    if (n == 0 || buf[n - 1] != '\n') printf("\n");
    return 0;
}
