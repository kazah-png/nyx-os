#include "libc.h"

/* cksum — print a CRC checksum and byte count for each file, like POSIX/GNU cksum.
 *   cksum [FILE...]
 * With no FILE operand (or "-") it reads stdin. The checksum is the POSIX CRC-32/CKSUM:
 * the bytes run through a CRC (polynomial 0x04C11DB7, MSB-first, no bit reflection), then
 * the byte count is appended low-order-byte-first, and the 32-bit result is complemented.
 * Output is "<crc> <nbytes>" for stdin, or "<crc> <nbytes> <file>" per file. */

static unsigned int crctab[256];

static void crc_init(void) {
    for (unsigned int n = 0; n < 256; n++) {
        unsigned int c = n << 24;
        for (int k = 0; k < 8; k++)
            c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : (c << 1);
        crctab[n] = c;
    }
}

static unsigned int crc_byte(unsigned int crc, unsigned char b) {
    return (crc << 8) ^ crctab[((crc >> 24) ^ b) & 0xFF];
}

/* CRC of one fd; sets *out_len to the byte count. */
static unsigned int cksum_fd(int fd, unsigned long* out_len) {
    unsigned int crc = 0;
    unsigned long len = 0;
    unsigned char buf[4096];
    for (;;) {
        int n = (int)read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) crc = crc_byte(crc, buf[i]);
        len += (unsigned long)n;
    }
    for (unsigned long l = len; l; l >>= 8)             // append the length, low byte first
        crc = crc_byte(crc, (unsigned char)(l & 0xFF));
    *out_len = len;
    return ~crc;
}

int main(int argc, char** argv) {
    crc_init();

    if (argc <= 1) {                                     // no operand: stdin, no name
        unsigned long len; unsigned int crc = cksum_fd(0, &len);
        printf("%u %u\n", crc, (unsigned int)len);
        return 0;
    }

    int rc = 0;
    for (int ai = 1; ai < argc; ai++) {
        if (argv[ai][0] == '-' && argv[ai][1] == '\0') { // "-" = stdin (no name, like GNU cksum)
            unsigned long len; unsigned int crc = cksum_fd(0, &len);
            printf("%u %u\n", crc, (unsigned int)len);
            continue;
        }
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("cksum: %s: cannot open\n", argv[ai]); rc = 1; continue; }
        unsigned long len; unsigned int crc = cksum_fd((int)f, &len);
        close((int)f);
        printf("%u %u %s\n", crc, (unsigned int)len, argv[ai]);
    }
    return rc;
}
