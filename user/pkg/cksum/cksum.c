#include "libc.h"
#include "syscall.h"

/* cksum — the POSIX CRC checksum, the classic file-integrity tool.
 *
 *   cksum <text...>   checksum the argument text (arguments joined by single spaces)
 *                     and print "CRC SIZE" (decimal CRC, then the byte count).
 *
 * The CRC-32/CKSUM variant: polynomial 0x04C11DB7 processed MSB-first (NOT reflected), the
 * running total seeded at 0, the message length appended as octets (least significant first),
 * then a final one's-complement. This differs from the zip/PNG CRC-32 (reflected 0xEDB88320)
 * and from CRC-32C, so it needs its own table. Standard algorithm, no upstream code; installs
 * via `xbm install cksum`.
 */

static unsigned int cksum_tab[256];
static void cksum_init(void) {
    for (unsigned int i = 0; i < 256; i++) {
        unsigned int c = i << 24;
        for (int k = 0; k < 8; k++)
            c = (c & 0x80000000u) ? ((c << 1) ^ 0x04C11DB7u) : (c << 1);
        cksum_tab[i] = c;
    }
}

static unsigned int cksum_crc(const unsigned char* d, unsigned long n) {
    unsigned int crc = 0;
    for (unsigned long i = 0; i < n; i++)
        crc = (crc << 8) ^ cksum_tab[((crc >> 24) ^ d[i]) & 0xFF];
    for (unsigned long len = n; len; len >>= 8)          /* append the length, octet LSB first */
        crc = (crc << 8) ^ cksum_tab[((crc >> 24) ^ (unsigned int)(len & 0xFF)) & 0xFF];
    return ~crc;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("Usage: cksum <text>\n"); return 1; }
    cksum_init();
    static unsigned char buf[8192];
    unsigned long n = 0;
    for (int a = 1; a < argc; a++) {
        if (a > 1 && n < sizeof(buf)) buf[n++] = ' ';
        for (const char* p = argv[a]; *p && n < sizeof(buf); p++) buf[n++] = (unsigned char)*p;
    }
    unsigned int crc = cksum_crc(buf, n);
    printf("%u %u\n", crc, (unsigned int)n);
    return 0;
}
