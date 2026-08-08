#include "libc.h"

/* sha256sum — print SHA-256 (FIPS 180-4) digests, like sha256sum(1):
 *   sha256sum [-b|-t] [FILE...]
 *   -b   read in binary mode (marks the line with '*' instead of ' ')
 * Each line is "<64 hex digits>  <name>" (two spaces = text mode) or
 * "<hex> *<name>" for -b. With no FILE (or "-") it hashes standard input as "-".
 * SHA-256 is implemented here in userspace (a .elf cannot call the kernel's). */

typedef unsigned int   u32;
typedef unsigned long long u64;

static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

typedef struct { u32 h[8]; u64 len; unsigned char buf[64]; int n; } ctx;

static u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }

static void init(ctx* c) {
    static const u32 iv[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    for (int i = 0; i < 8; i++) c->h[i] = iv[i];
    c->len = 0; c->n = 0;
}

static void block(ctx* c, const unsigned char* p) {
    u32 w[64];
    for (int i = 0; i < 16; i++) w[i] = ((u32)p[4*i] << 24) | ((u32)p[4*i+1] << 16) | ((u32)p[4*i+2] << 8) | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        u32 s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = h + S1 + ch + K[i] + w[i];
        u32 S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        u32 maj = (a & b) ^ (a & cc) ^ (b & cc);
        u32 t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
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

static void final(ctx* c, unsigned char out[32]) {
    u64 bits = c->len * 8;
    c->buf[c->n++] = 0x80;
    if (c->n > 56) { while (c->n < 64) c->buf[c->n++] = 0; block(c, c->buf); c->n = 0; }
    while (c->n < 56) c->buf[c->n++] = 0;
    for (int i = 0; i < 8; i++) c->buf[56 + i] = (unsigned char)(bits >> (56 - 8*i));
    block(c, c->buf);
    for (int i = 0; i < 8; i++) { out[4*i]=(unsigned char)(c->h[i]>>24); out[4*i+1]=(unsigned char)(c->h[i]>>16); out[4*i+2]=(unsigned char)(c->h[i]>>8); out[4*i+3]=(unsigned char)c->h[i]; }
}

static int binmode = 0;

static void hash_fd(int fd, const char* name) {
    ctx c; init(&c);
    unsigned char buf[8192]; int n;
    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) update(&c, buf, (u32)n);
    unsigned char d[32]; final(&c, d);
    static const char HEX[] = "0123456789abcdef";
    char line[80]; int o = 0;
    for (int i = 0; i < 32; i++) { line[o++] = HEX[d[i] >> 4]; line[o++] = HEX[d[i] & 15]; }
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
            else { printf("sha256sum: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (i >= argc) { hash_fd(0, "-"); return 0; }
    int rc = 0;
    for (; i < argc; i++) {
        if (argv[i][0] == '-' && !argv[i][1]) { hash_fd(0, "-"); continue; }
        long f = open(argv[i], O_RDONLY, 0);
        if (f < 0) { printf("sha256sum: %s: cannot open\n", argv[i]); rc = 1; continue; }
        hash_fd((int)f, argv[i]);
        close((int)f);
    }
    return rc;
}
