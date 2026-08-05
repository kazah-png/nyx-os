#include "libc.h"

/* base64 — encode (default) or decode (-d) a file or stdin, like base64(1).
 *   base64 [-d] [-w COLS] [FILE]
 *   -d        decode instead of encode
 *   -w COLS   wrap encoded output at COLS chars (default 76; 0 = one long line)
 * Encoding is standard RFC 4648 ('+' '/' alphabet, '=' padding). Decoding is
 * STRICT on the Base64 content — it rejects stray symbols, misplaced padding and
 * non-canonical trailing bits — but, like GNU base64, it tolerates and skips
 * whitespace (newlines, spaces, tabs) so it round-trips its own wrapped output.
 * This is the userland companion to the kernel's base64 primitive (v6.4.103); a
 * ring-3 tool can't call the kernel copy, so the codec lives here too. */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Buffered stdout so we don't pay a syscall per character. */
static char wbuf[1024];
static int  wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(char c) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = c; }

/* Slurp a whole fd into a grown heap buffer (Base64 inputs — keys, certs, small
 * blobs — are modest; realloc covers the occasional large file). */
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

static void emit4(const char q[4], int* col, int wrap) {
    for (int k = 0; k < 4; k++) {
        wput(q[k]);
        if (wrap > 0 && ++(*col) == wrap) { wput('\n'); *col = 0; }
    }
}

static void encode(const unsigned char* in, unsigned int n, int wrap) {
    char q[4];
    int col = 0;
    unsigned int i = 0;
    for (; i + 3 <= n; i += 3) {
        unsigned int v = ((unsigned int)in[i] << 16) | ((unsigned int)in[i+1] << 8) | in[i+2];
        q[0] = B64[(v>>18)&63]; q[1] = B64[(v>>12)&63];
        q[2] = B64[(v>>6)&63];  q[3] = B64[v&63];
        emit4(q, &col, wrap);
    }
    unsigned int rem = n - i;
    if (rem == 1) {
        unsigned int v = (unsigned int)in[i] << 16;
        q[0] = B64[(v>>18)&63]; q[1] = B64[(v>>12)&63]; q[2] = '='; q[3] = '=';
        emit4(q, &col, wrap);
    } else if (rem == 2) {
        unsigned int v = ((unsigned int)in[i] << 16) | ((unsigned int)in[i+1] << 8);
        q[0] = B64[(v>>18)&63]; q[1] = B64[(v>>12)&63]; q[2] = B64[(v>>6)&63]; q[3] = '=';
        emit4(q, &col, wrap);
    }
    if (wrap == 0 || col != 0) wput('\n');   // exactly one trailing newline
    wflush();
}

/* Decode one 4-symbol quartet strictly; write 1..3 bytes; return 0 ok / -1 bad.
 * *pad becomes 1 if the quartet carried padding (i.e. it must be the last one). */
static int dec_quartet(const char q[4], int* pad) {
    int v0 = b64val(q[0]), v1 = b64val(q[1]);
    if (v0 < 0 || v1 < 0) return -1;
    int p2 = (q[2] == '='), p3 = (q[3] == '=');
    if (p2 && !p3) return -1;
    int v2 = p2 ? 0 : b64val(q[2]);
    int v3 = p3 ? 0 : b64val(q[3]);
    if (v2 < 0 || v3 < 0) return -1;
    unsigned int t = ((unsigned int)v0<<18) | ((unsigned int)v1<<12) | ((unsigned int)v2<<6) | (unsigned int)v3;
    if (p2) {                       // 1 output byte; v1 low 4 bits unused
        if (v1 & 0x0F) return -1;
        wput((char)(t>>16));
    } else if (p3) {                // 2 output bytes; v2 low 2 bits unused
        if (v2 & 0x03) return -1;
        wput((char)(t>>16)); wput((char)(t>>8));
    } else {                        // 3 output bytes
        wput((char)(t>>16)); wput((char)(t>>8)); wput((char)t);
    }
    *pad = (p2 || p3);
    return 0;
}

static int decode(const unsigned char* in, unsigned int n) {
    char q[4];
    int qn = 0, done = 0;
    for (unsigned int i = 0; i < n; i++) {
        char c = (char)in[i];
        if (c=='\n' || c=='\r' || c=='\t' || c==' ') continue;   // skip whitespace
        if (done) return -1;                                     // data after a padded final group
        q[qn++] = c;
        if (qn == 4) { int pad = 0; if (dec_quartet(q, &pad)) return -1; if (pad) done = 1; qn = 0; }
    }
    if (qn != 0) return -1;          // truncated: not a whole number of quartets
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
        } else { printf("base64: invalid option %s\n", a); return 1; }
    }

    int fd = 0;
    if (ai < argc) {
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("base64: %s: not found\n", argv[ai]); return 1; }
        fd = (int)f;
    }

    unsigned int n = 0;
    unsigned char* buf = read_all(fd, &n);
    if (fd != 0) close(fd);
    if (!buf) { printf("base64: out of memory\n"); return 1; }

    int rc = 0;
    if (dec) {
        if (decode(buf, n) != 0) { printf("base64: invalid input\n"); rc = 1; }
    } else {
        encode(buf, n, wrap);
    }
    free(buf);
    return rc;
}
