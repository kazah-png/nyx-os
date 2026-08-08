#include "libc.h"

/* sha512sum — print SHA-512 (FIPS 180-4) digests, like sha512sum(1):
 *   sha512sum [-b|-t] [FILE...]
 * Line format "<128 hex digits>  <name>" (two spaces = text) or " *" for -b.
 * With no FILE (or "-") it hashes standard input as "-". SHA-512 is implemented
 * here in userspace (a .elf cannot call the kernel's). Companion to sha256sum. */

typedef unsigned long long u64;

static const u64 IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};
static const u64 K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

typedef struct { u64 h[8]; u64 len_lo, len_hi; unsigned char buf[128]; int n; } ctx;

static u64 rotr(u64 x, int n) { return (x >> n) | (x << (64 - n)); }

static void init(ctx* c) { for (int i = 0; i < 8; i++) c->h[i] = IV[i]; c->len_lo = c->len_hi = 0; c->n = 0; }

static void block(ctx* c, const unsigned char* p) {
    u64 w[80];
    for (int i = 0; i < 16; i++) { u64 v = 0; for (int j = 0; j < 8; j++) v = (v << 8) | p[8*i + j]; w[i] = v; }
    for (int i = 16; i < 80; i++) {
        u64 s0 = rotr(w[i-15], 1) ^ rotr(w[i-15], 8) ^ (w[i-15] >> 7);
        u64 s1 = rotr(w[i-2], 19) ^ rotr(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u64 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for (int i = 0; i < 80; i++) {
        u64 S1 = rotr(e,14) ^ rotr(e,18) ^ rotr(e,41);
        u64 ch = (e & f) ^ (~e & g);
        u64 t1 = h + S1 + ch + K[i] + w[i];
        u64 S0 = rotr(a,28) ^ rotr(a,34) ^ rotr(a,39);
        u64 maj = (a & b) ^ (a & cc) ^ (b & cc);
        u64 t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}

static void update(ctx* c, const unsigned char* p, u64 n) {
    u64 old = c->len_lo; c->len_lo += n; if (c->len_lo < old) c->len_hi++;
    while (n) {
        int take = 128 - c->n; if ((u64)take > n) take = n;
        for (int i = 0; i < take; i++) c->buf[c->n + i] = p[i];
        c->n += take; p += take; n -= take;
        if (c->n == 128) { block(c, c->buf); c->n = 0; }
    }
}

static void final(ctx* c, unsigned char out[64]) {
    u64 bits_hi = (c->len_hi << 3) | (c->len_lo >> 61);
    u64 bits_lo = c->len_lo << 3;
    c->buf[c->n++] = 0x80;
    if (c->n > 112) { while (c->n < 128) c->buf[c->n++] = 0; block(c, c->buf); c->n = 0; }
    while (c->n < 112) c->buf[c->n++] = 0;
    for (int i = 0; i < 8; i++) c->buf[112 + i] = (unsigned char)(bits_hi >> (56 - 8*i));
    for (int i = 0; i < 8; i++) c->buf[120 + i] = (unsigned char)(bits_lo >> (56 - 8*i));
    block(c, c->buf);
    for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) out[8*i + j] = (unsigned char)(c->h[i] >> (56 - 8*j));
}

static int binmode = 0;

static void hash_fd(int fd, const char* name) {
    ctx c; init(&c);
    unsigned char buf[8192]; int n;
    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) update(&c, buf, (u64)n);
    unsigned char d[64]; final(&c, d);
    static const char HEX[] = "0123456789abcdef";
    char line[160]; int o = 0;
    for (int i = 0; i < 64; i++) { line[o++] = HEX[d[i] >> 4]; line[o++] = HEX[d[i] & 15]; }
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
            else { printf("sha512sum: invalid option -- '%c'\n", *p); return 1; }
        }
    }
    if (i >= argc) { hash_fd(0, "-"); return 0; }
    int rc = 0;
    for (; i < argc; i++) {
        if (argv[i][0] == '-' && !argv[i][1]) { hash_fd(0, "-"); continue; }
        long f = open(argv[i], O_RDONLY, 0);
        if (f < 0) { printf("sha512sum: %s: cannot open\n", argv[i]); rc = 1; continue; }
        hash_fd((int)f, argv[i]);
        close((int)f);
    }
    return rc;
}
