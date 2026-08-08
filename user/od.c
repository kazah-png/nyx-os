#include "libc.h"

/* od — dump files in octal/hex/decimal/char, like od(1).
 *   od [-A o|d|x|n] [-t TYPE] [-N BYTES] [-j SKIP] [-v] [-c|-b|-x|-o|-d] [FILE...]
 * Default is GNU's default: -A o -t o2 (octal address, 2-byte little-endian octal
 * words, 16 bytes per line). TYPE is one of o/x/d/u with a size 1 or 2, or c
 * (char). Short aliases: -c=char, -b=o1, -x=x2, -o=o2, -d=u2. Consecutive
 * identical 16-byte lines collapse to a single "*" (unless -v). The final line is
 * the total offset. With no FILE (or "-") od reads standard input. */

/* ---- input: slurp files / stdin into one growable buffer ---- */
static unsigned char* g_buf; static long g_len, g_cap;
static void append_fd(int fd) {
    unsigned char tmp[4096]; int n;
    while ((n = (int)read(fd, tmp, sizeof(tmp))) > 0) {
        if (g_len + n > g_cap) {
            long nc = g_cap ? g_cap : 65536;
            while (nc < g_len + n) nc *= 2;
            g_buf = realloc(g_buf, nc); g_cap = nc;
        }
        memcpy(g_buf + g_len, tmp, n); g_len += n;
    }
}

/* ---- fixed-width numeric formatters (return chars written) ---- */
static int put_oct(char* o, unsigned long v, int d) { for (int i = d - 1; i >= 0; i--) { o[i] = (char)('0' + (v & 7)); v >>= 3; } return d; }
static int put_hex(char* o, unsigned long v, int d) { const char* h = "0123456789abcdef"; for (int i = d - 1; i >= 0; i--) { o[i] = h[v & 15]; v >>= 4; } return d; }
static int put_dec0(char* o, unsigned long v, int d) { char t[24]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } int pad = d - n; for (int i = 0; i < pad; i++) o[i] = '0'; for (int i = 0; i < n; i++) o[(pad > 0 ? pad : 0) + i] = t[n - 1 - i]; return d > n ? d : n; }
static int put_udec(char* o, unsigned long v, int w) { char t[24]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10); v /= 10; } int pad = w - n; for (int i = 0; i < pad; i++) o[i] = ' '; for (int i = 0; i < n; i++) o[(pad > 0 ? pad : 0) + i] = t[n - 1 - i]; return w > n ? w : n; }
static int put_sdec(char* o, long sv, int w) { char t[24]; int n = 0, neg = sv < 0; unsigned long a = neg ? (unsigned long)(-sv) : (unsigned long)sv; if (!a) t[n++] = '0'; while (a) { t[n++] = (char)('0' + a % 10); a /= 10; } if (neg) t[n++] = '-'; int pad = w - n; for (int i = 0; i < pad; i++) o[i] = ' '; for (int i = 0; i < n; i++) o[(pad > 0 ? pad : 0) + i] = t[n - 1 - i]; return w > n ? w : n; }

/* config */
static int   OT_unit = 2, OT_base = 8, OT_width = 6, OT_sgn = 0, OT_char = 0;   /* default o2 */
static char  ADDR = 'o';
static long  OPT_N = -1, OPT_J = 0; static int OPT_V = 0;

static void set_type(char t, int size) {
    OT_char = 0; OT_unit = size; OT_sgn = 0;
    if (t == 'o') { OT_base = 8;  OT_width = size == 1 ? 3 : 6; }
    else if (t == 'x') { OT_base = 16; OT_width = size == 1 ? 2 : 4; }
    else if (t == 'u') { OT_base = 10; OT_width = size == 1 ? 3 : 5; }
    else if (t == 'd') { OT_base = 10; OT_sgn = 1; OT_width = size == 1 ? 4 : 6; }
    else if (t == 'c') { OT_char = 1; OT_unit = 1; }
}

static int emit_unit(char* o, unsigned long v) {
    if (OT_base == 8)  return put_oct(o, v, OT_width);
    if (OT_base == 16) return put_hex(o, v, OT_width);
    if (OT_sgn) { long sv = OT_unit == 1 ? (long)(signed char)v : (long)(short)v; return put_sdec(o, sv, OT_width); }
    return put_udec(o, v, OT_width);
}

/* -c: each byte as a named escape / printable char / 3-digit octal, in a 3-wide
 * right-justified field. */
static int emit_char(char* o, unsigned char c) {
    char rep[4]; int rl;
    const char* e = 0;
    switch (c) { case 0: e = "\\0"; break; case 7: e = "\\a"; break; case 8: e = "\\b"; break;
                 case 9: e = "\\t"; break; case 10: e = "\\n"; break; case 11: e = "\\v"; break;
                 case 12: e = "\\f"; break; case 13: e = "\\r"; break; }
    if (e) { rep[0] = e[0]; rep[1] = e[1]; rl = 2; }
    else if (c >= 32 && c < 127) { rep[0] = (char)c; rl = 1; }
    else { rep[0] = (char)('0' + ((c >> 6) & 7)); rep[1] = (char)('0' + ((c >> 3) & 7)); rep[2] = (char)('0' + (c & 7)); rl = 3; }
    int pad = 3 - rl; for (int i = 0; i < pad; i++) o[i] = ' '; for (int i = 0; i < rl; i++) o[pad + i] = rep[i]; return 3;
}

static int put_addr(char* o, unsigned long off) {
    if (ADDR == 'o') return put_oct(o, off, 7);
    if (ADDR == 'd') return put_dec0(o, off, 7);
    if (ADDR == 'x') return put_hex(o, off, 6);
    return 0;   /* 'n' */
}

static int parse_num(const char* s, long* out) {
    char* end; unsigned long v = strtoul(s, &end, 0);
    if (end == s || *end) return -1;
    *out = (long)v; return 0;
}

int main(int argc, char** argv) {
    const char* files[64]; int nf = 0;
    int only_files = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (only_files || a[0] != '-' || a[1] == '\0') { if (nf < 64) files[nf++] = a; continue; }
        if (a[1] == '-' && a[2] == '\0') { only_files = 1; continue; }
        char f = a[1];
        /* value-less flags first — they must NOT consume the next argument */
        if      (f == 'v') { OPT_V = 1; continue; }
        else if (f == 'c') { set_type('c', 1); continue; }
        else if (f == 'b') { set_type('o', 1); continue; }
        else if (f == 'o') { set_type('o', 2); continue; }
        else if (f == 'x') { set_type('x', 2); continue; }
        else if (f == 'd') { set_type('u', 2); continue; }
        /* value-taking flags: value is the rest of the token or the next argument */
        const char* val = a[2] ? a + 2 : (i + 1 < argc ? argv[++i] : 0);
        if (f == 'A') { if (!val || !strchr("odxn", val[0])) { printf("od: bad -A\n"); return 1; } ADDR = val[0]; }
        else if (f == 'N') { if (!val || parse_num(val, &OPT_N) < 0) { printf("od: bad -N\n"); return 1; } }
        else if (f == 'j') { if (!val || parse_num(val, &OPT_J) < 0) { printf("od: bad -j\n"); return 1; } }
        else if (f == 't') {
            if (!val || !strchr("oxduc", val[0])) { printf("od: bad -t\n"); return 1; }
            int size = 1; char ty = val[0];
            if (ty != 'c') size = (val[1] == '1') ? 1 : 2;   /* size 1 or 2; default 2 */
            set_type(ty, size);
        }
        else { printf("od: invalid option -- '%c'\n", f); return 1; }
    }

    if (nf == 0) append_fd(0);
    else for (int i = 0; i < nf; i++) {
        if (files[i][0] == '-' && files[i][1] == '\0') { append_fd(0); continue; }
        long fd = open(files[i], O_RDONLY, 0);
        if (fd < 0) { printf("od: %s: cannot open\n", files[i]); continue; }
        append_fd((int)fd); close((int)fd);
    }

    long start = OPT_J > g_len ? g_len : OPT_J;
    long end = g_len;
    if (OPT_N >= 0 && start + OPT_N < end) end = start + OPT_N;

    char line[256];
    unsigned char prev[16]; int have_prev = 0, star = 0;
    long off = start;
    while (off < end) {
        int cnt = (int)(end - off); if (cnt > 16) cnt = 16;
        if (!OPT_V && have_prev && cnt == 16 && memcmp(g_buf + off, prev, 16) == 0) {
            if (!star) { write(1, "*\n", 2); star = 1; }
            memcpy(prev, g_buf + off, 16); off += 16; continue;
        }
        star = 0;
        int p = 0;
        if (ADDR != 'n') p += put_addr(line + p, (unsigned long)off);
        for (int b = 0; b < cnt; b += OT_unit) {
            line[p++] = ' ';
            if (OT_char) { p += emit_char(line + p, g_buf[off + b]); }
            else {
                unsigned long v = 0;
                for (int k = 0; k < OT_unit; k++) { unsigned long by = (b + k < cnt) ? g_buf[off + b + k] : 0; v |= by << (8 * k); }
                p += emit_unit(line + p, v);
            }
        }
        line[p++] = '\n';
        write(1, line, p);
        if (cnt == 16) { memcpy(prev, g_buf + off, 16); have_prev = 1; } else have_prev = 0;
        off += cnt;
    }
    if (ADDR != 'n') { int p = put_addr(line, (unsigned long)end); line[p++] = '\n'; write(1, line, p); }
    return 0;
}
