#include "libc.h"

/* md5sum — print MD5 (RFC 1321) digests, like md5sum(1):
 *   md5sum [-b|-t] [FILE...]
 *   -b   read in binary mode (marks the line with '*' instead of ' ')
 * Each line is "<32 hex digits>  <name>" (two spaces = text mode) or
 * "<hex> *<name>" for -b. With no FILE (or "-") it hashes standard input as "-".
 * MD5 is collision-broken and is NOT for security — this is a content checksum for
 * interop with the many tools that still emit MD5. Implemented here in userspace (a
 * .elf cannot call the kernel's md5.c); the algorithm mirrors it (little-endian). */

typedef unsigned int   u32;
typedef unsigned long long u64;

/* Per-round left-rotate amounts (RFC 1321 3.4). */
static const unsigned char S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

/* K[i] = floor(2^32 * abs(sin(i + 1))). */
static const u32 K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

typedef struct { u32 h[4]; u64 len; unsigned char buf[64]; int n; } ctx;

static u32 rotl(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

static void init(ctx* c) {
    c->h[0] = 0x67452301; c->h[1] = 0xefcdab89;
    c->h[2] = 0x98badcfe; c->h[3] = 0x10325476;
    c->len = 0; c->n = 0;
}

static void block(ctx* c, const unsigned char* p) {
    u32 m[16];
    for (int i = 0; i < 16; i++)                 // MD5 reads the block little-endian
        m[i] = (u32)p[4*i] | ((u32)p[4*i+1] << 8) | ((u32)p[4*i+2] << 16) | ((u32)p[4*i+3] << 24);
    u32 a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    for (int i = 0; i < 64; i++) {
        u32 f, g;
        if      (i < 16) { f = (b & cc) | (~b & d);       g = i;              }
        else if (i < 32) { f = (d & b) | (~d & cc);       g = (5*i + 1) & 15; }
        else if (i < 48) { f = b ^ cc ^ d;                g = (3*i + 5) & 15; }
        else             { f = cc ^ (b | ~d);             g = (7*i)     & 15; }
        f = f + a + K[i] + m[g];
        a = d; d = cc; cc = b;
        b = b + rotl(f, S[i]);
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
}

static void update(ctx* c, const unsigned char* p, u32 n) {
    c->len += n;
    while (n) {
        int take = 64 - c->n; if ((u32)take > n) take = n;
        for (int i = 0; i < take; i++) c->buf[c->n + i] = p[i];
        c->n += take; p += take; n -= take;
        if (c->n == 64) { block(c, c->buf); c->n = 0; }
    }
}

static void final(ctx* c, unsigned char out[16]) {
    u64 bits = c->len * 8;
    c->buf[c->n++] = 0x80;
    if (c->n > 56) { while (c->n < 64) c->buf[c->n++] = 0; block(c, c->buf); c->n = 0; }
    while (c->n < 56) c->buf[c->n++] = 0;
    for (int i = 0; i < 8; i++) c->buf[56 + i] = (unsigned char)(bits >> (8*i));   // little-endian length
    block(c, c->buf);
    for (int i = 0; i < 4; i++) {                                                   // little-endian output
        out[4*i]     = (unsigned char)(c->h[i]);
        out[4*i + 1] = (unsigned char)(c->h[i] >> 8);
        out[4*i + 2] = (unsigned char)(c->h[i] >> 16);
        out[4*i + 3] = (unsigned char)(c->h[i] >> 24);
    }
}

static int binmode = 0;

static void hash_fd(int fd, const char* name) {
    ctx c; init(&c);
    unsigned char buf[8192]; int n;
    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) update(&c, buf, (u32)n);
    unsigned char d[16]; final(&c, d);
    static const char HEX[] = "0123456789abcdef";
    char line[64]; int o = 0;
    for (int i = 0; i < 16; i++) { line[o++] = HEX[d[i] >> 4]; line[o++] = HEX[d[i] & 15]; }
    line[o++] = ' ';
    line[o++] = binmode ? '*' : ' ';
    write(1, line, o);
    write(1, name, (int)strlen(name));
    write(1, "\n", 1);
}

int main(int argc, char** argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "--binary")) { binmode = 1; continue; }
        if (!strcmp(argv[i], "--text"))   { binmode = 0; continue; }
        for (const char* p = argv[i] + 1; *p; p++) {
            if (*p == 'b') binmode = 1;
            else if (*p == 't') binmode = 0;
            else { printf("md5sum: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (i >= argc) { hash_fd(0, "-"); return 0; }
    int rc = 0;
    for (; i < argc; i++) {
        if (argv[i][0] == '-' && !argv[i][1]) { hash_fd(0, "-"); continue; }
        long f = open(argv[i], O_RDONLY, 0);
        if (f < 0) { printf("md5sum: %s: cannot open\n", argv[i]); rc = 1; continue; }
        hash_fd((int)f, argv[i]);
        close((int)f);
    }
    return rc;
}
