#include "libc.h"

/* sum — checksum and count the blocks of each file, like the classic sum(1). Two
 * historical algorithms select the checksum and the block size:
 *   -r   BSD: a 16-bit right-rotate-then-add checksum over 1024-byte blocks (default)
 *   -s   System V: the byte total folded down to 16 bits, over 512-byte blocks
 * Output is "<checksum> <blocks>" — BSD right-justifies the block count in a width-5
 * field, System V uses a single space — plus a trailing " <file>" for each named
 * operand. With no operand it reads standard input (so does a "-" operand, which is
 * then echoed as the name). This is the older, simpler pair the CRC-based cksum
 * replaced; kept for scripts and archives that still speak it. */

static int sput(char* b, int o, const char* s) { while (*s) b[o++] = *s++; return o; }
static int uput(char* b, int o, unsigned long v) {           // decimal, no padding
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    while (n) b[o++] = t[--n];
    return o;
}
static int uput_w(char* b, int o, unsigned long v, int width) {   // right-justified in `width`
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for (int pad = width - n; pad > 0; pad--) b[o++] = ' ';
    while (n) b[o++] = t[--n];
    return o;
}
static int uput_z5(char* b, int o, unsigned long v) {        // exactly 5 digits, zero-padded
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + (int)(v % 10)); v /= 10; }
    for (int pad = 5 - n; pad > 0; pad--) b[o++] = '0';
    while (n) b[o++] = t[--n];
    return o;
}

/* Fold one file descriptor's bytes with the chosen algorithm. Returns 0, or -1 on a
 * read error; *ck / *blocks receive the 16-bit checksum and the block count. */
static int sum_fd(int fd, int sysv, unsigned int* ck, unsigned long* blocks) {
    unsigned char buf[4096];
    unsigned long total = 0;
    unsigned int bsd = 0;              // 16-bit BSD rotate checksum
    unsigned int sv  = 0;              // System V running sum (uint32 wraps mod 2^32)
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            unsigned int b = buf[i];
            if (sysv) {
                sv = (sv + b) & 0xFFFFFFFFu;
            } else {
                bsd = ((bsd >> 1) | ((bsd & 1) << 15)) & 0xFFFF;   // rotate right 1 bit
                bsd = (bsd + b) & 0xFFFF;
            }
        }
        total += (unsigned long)n;
    }
    if (n < 0) return -1;
    if (sysv) {
        unsigned int r = (sv & 0xFFFF) + ((sv >> 16) & 0xFFFF);
        r = (r & 0xFFFF) + (r >> 16);                              // fold twice to 16 bits
        *ck = r & 0xFFFF;
        *blocks = (total + 511) / 512;
    } else {
        *ck = bsd & 0xFFFF;
        *blocks = (total + 1023) / 1024;
    }
    return 0;
}

/* Emit one result line. `name` is NULL for the no-operand stdin case. */
static void emit(unsigned int ck, unsigned long blocks, int sysv, const char* name) {
    char m[320]; int o = 0;
    o = sysv ? uput(m, o, ck) : uput_z5(m, o, ck);   // BSD zero-pads the checksum to 5 digits
    m[o++] = ' ';
    o = sysv ? uput(m, o, blocks) : uput_w(m, o, blocks, 5);
    if (name) { m[o++] = ' '; o = sput(m, o, name); }
    m[o++] = '\n';
    write(1, m, o);
}

int main(int argc, char** argv) {
    int sysv = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == '-' && a[2] == '\0') { ai++; break; }         // "--" ends options
        int ok = 1;
        for (int k = 1; a[k]; k++) {
            if (a[k] == 's') sysv = 1;
            else if (a[k] == 'r') { /* BSD is the default; -r never overrides an earlier -s */ }
            else { ok = 0; break; }
        }
        if (!ok) {
            char m[128]; int o = sput(m, 0, "sum: invalid option "); o = sput(m, o, a); m[o++] = '\n';
            write(2, m, o); return 2;
        }
    }

    if (argc - ai <= 0) {                                          // no operand: stdin, no name
        unsigned int ck; unsigned long blk;
        if (sum_fd(0, sysv, &ck, &blk) < 0) { const char* e = "sum: -: read error\n"; write(2, e, (int)strlen(e)); return 2; }
        emit(ck, blk, sysv, 0);
        return 0;
    }

    int status = 0;
    for (; ai < argc; ai++) {
        const char* f = argv[ai];
        int fd;
        if (f[0] == '-' && f[1] == '\0') fd = 0;
        else {
            long g = open(f, O_RDONLY, 0);
            if (g < 0) {
                char m[300]; int o = sput(m, 0, "sum: "); o = sput(m, o, f); o = sput(m, o, ": No such file or directory\n");
                write(2, m, o); status = 2; continue;
            }
            fd = (int)g;
        }
        unsigned int ck; unsigned long blk;
        if (sum_fd(fd, sysv, &ck, &blk) < 0) {
            char m[300]; int o = sput(m, 0, "sum: "); o = sput(m, o, f); o = sput(m, o, ": read error\n");
            write(2, m, o); if (fd) close(fd); status = 2; continue;
        }
        if (fd) close(fd);
        emit(ck, blk, sysv, (f[0] == '-' && f[1] == '\0') ? 0 : f);   // a "-" operand prints no name
    }
    return status;
}
