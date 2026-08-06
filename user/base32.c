#include "libc.h"

/* base32 — encode (default) or decode (-d) a file or stdin, like base32(1).
 *   base32 [-d] [-w COLS] [FILE]
 *   -d        decode instead of encode
 *   -w COLS   wrap encoded output at COLS chars (default 76; 0 = one long line)
 * Encoding is standard RFC 4648 section 6 (the A-Z2-7 alphabet, '=' padding; each
 * 5-byte group becomes 8 symbols, a trailing partial group emits 2/4/5/7 data
 * symbols + padding). Decoding is STRICT on the Base32 content — it rejects stray
 * symbols, a bad padding count (only 0/1/3/4/6 '=' can occur), misplaced padding
 * and non-canonical trailing bits — but, like GNU base32, it tolerates and skips
 * whitespace (newlines, spaces, tabs) so it round-trips its own wrapped output.
 * The userland companion to base64 (and to the kernel's base32 primitive, v6.4.117);
 * a ring-3 tool can't call the kernel copy, so the codec lives here too. */

static const char B32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int b32val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

/* Buffered stdout so we don't pay a syscall per character. */
static char wbuf[1024];
static int  wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(char c) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = c; }

/* Slurp a whole fd into a grown heap buffer (Base32 inputs — keys, TOTP secrets,
 * small blobs — are modest; realloc covers the occasional large file). */
static unsigned char* read_all(int fd, unsigned int* out_len) {
    unsigned int cap = 8192, n = 0;
    unsigned char* buf = (unsigned char*)malloc(cap);
    if (!buf) return 0;
    for (;;) {
        if (n == cap) {
            unsigned int nc = cap * 2;
            unsigned char* nb = (unsigned char*)realloc(buf, nc);
            if (!nb) { free(buf); return 0; }
            buf = nb; cap = nc;
        }
        long r = read(fd, buf + n, cap - n);
        if (r <= 0) break;
        n += (unsigned int)r;
    }
    *out_len = n;
    return buf;
}

static void emit8(const char oct[8], int* col, int wrap) {
    for (int k = 0; k < 8; k++) {
        wput(oct[k]);
        if (wrap > 0 && ++(*col) == wrap) { wput('\n'); *col = 0; }
    }
}

static void encode(const unsigned char* in, unsigned int n, int wrap) {
    char oct[8];
    int col = 0;
    unsigned int i = 0;
    for (; i + 5 <= n; i += 5) {
        unsigned long v = ((unsigned long)in[i]   << 32) | ((unsigned long)in[i+1] << 24) |
                          ((unsigned long)in[i+2] << 16) | ((unsigned long)in[i+3] << 8)  |
                          ((unsigned long)in[i+4]);
        for (int k = 0; k < 8; k++) oct[k] = B32[(v >> (5 * (7 - k))) & 31];
        emit8(oct, &col, wrap);
    }
    unsigned int rem = n - i;
    if (rem) {
        /* Pack the remaining 1..4 bytes big-endian into the top of a 40-bit value. */
        unsigned long v = 0;
        for (unsigned int k = 0; k < rem; k++)
            v |= (unsigned long)in[i + k] << (32 - 8 * k);
        /* data symbols carried = ceil(rem*8 / 5): 1->2, 2->4, 3->5, 4->7. */
        static const int nsym_for[5] = { 0, 2, 4, 5, 7 };
        int nsym = nsym_for[rem];
        for (int k = 0; k < nsym; k++) oct[k] = B32[(v >> (5 * (7 - k))) & 31];
        for (int k = nsym; k < 8; k++) oct[k] = '=';
        emit8(oct, &col, wrap);
    }
    // GNU terminates each wrapped line (incl. the last) with '\n', so a partial final
    // line gets one; but with -w0 (no wrapping) it emits NO trailing newline at all,
    // and empty input yields empty output (col stays 0).
    if (wrap != 0 && col != 0) wput('\n');
    wflush();
}

/* Decode one 8-symbol group strictly; write 1..5 bytes; return 0 ok / -1 bad.
 * *pad becomes 1 if the group carried padding (i.e. it must be the last one). */
static int dec_octet(const char g[8], int* pad) {
    int np = 0;
    while (np < 8 && g[7 - np] == '=') np++;
    /* valid '=' counts are 0/1/3/4/6 -> 5/4/3/2/1 output bytes; 2/5/7/8 are illegal */
    static const signed char nbytes_for_pad[7] = { 5, 4, -1, 3, 2, -1, 1 };
    if (np > 6 || nbytes_for_pad[np] < 0) return -1;
    int nsym = 8 - np;
    unsigned long v = 0;
    for (int k = 0; k < nsym; k++) {
        int s = b32val(g[k]);
        if (s < 0) return -1;                    // non-alphabet (a '=' before the pad run fails here too)
        v |= (unsigned long)s << (5 * (7 - k));
    }
    int nbytes = nbytes_for_pad[np];
    int unused = 40 - 8 * nbytes;                // the low bits the data symbols overhang
    if (unused > 0 && (v & (((unsigned long)1 << unused) - 1)) != 0) return -1;  // non-canonical
    for (int k = 0; k < nbytes; k++) wput((char)(v >> (32 - 8 * k)));
    *pad = (np > 0);
    return 0;
}

static int decode(const unsigned char* in, unsigned int n) {
    char g[8];
    int gn = 0, done = 0;
    for (unsigned int i = 0; i < n; i++) {
        char c = (char)in[i];
        if (c=='\n' || c=='\r' || c=='\t' || c==' ') continue;   // skip whitespace
        if (done) return -1;                                     // data after a padded final group
        g[gn++] = c;
        if (gn == 8) { int pad = 0; if (dec_octet(g, &pad)) return -1; if (pad) done = 1; gn = 0; }
    }
    if (gn != 0) return -1;          // truncated: not a whole number of 8-symbol groups
    wflush();
    return 0;
}

int main(int argc, char** argv) {
    int dec = 0, wrap = 76, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == 'd' && !a[2]) dec = 1;
        else if (a[1] == 'w') {
            const char* v = a[2] ? a+2 : (ai+1 < argc ? argv[++ai] : 0);
            if (v) { wrap = atoi(v); if (wrap < 0) wrap = 0; }
        } else { printf("base32: invalid option %s\n", a); return 1; }
    }

    int fd = 0;
    if (ai < argc) {
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("base32: %s: not found\n", argv[ai]); return 1; }
        fd = (int)f;
    }

    unsigned int n = 0;
    unsigned char* buf = read_all(fd, &n);
    if (fd != 0) close(fd);
    if (!buf) { printf("base32: out of memory\n"); return 1; }

    int rc = 0;
    if (dec) {
        if (decode(buf, n) != 0) { printf("base32: invalid input\n"); rc = 1; }
    } else {
        encode(buf, n, wrap);
    }
    free(buf);
    return rc;
}
