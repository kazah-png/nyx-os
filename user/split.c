#include "libc.h"

// O_WRONLY isn't defined on NyxOS (fds are read/write regardless of an access-mode
// bit); define it to 0 so the same source also builds host-side for the diff test.
#ifndef O_WRONLY
#define O_WRONLY 0
#endif

/* split — split a file into pieces, like split(1):
 *   split [-l N | -b SIZE] [-a LEN] [-d] [INPUT [PREFIX]]
 *     -l N     N lines per output file (default 1000)
 *     -b SIZE  SIZE bytes per output file (SIZE may end in k/m/g = *1024^n)
 *     -a LEN   suffix length (default 2)
 *     -d       numeric suffixes (00,01,…) instead of alphabetic (aa,ab,…)
 * Pieces are PREFIX + suffix (default PREFIX "x" -> xaa, xab, …). With no INPUT
 * (or "-") it reads standard input. */

static unsigned char* g_buf; static long g_len, g_cap;
static void append_fd(int fd) {
    unsigned char tmp[8192]; int n;
    while ((n = (int)read(fd, tmp, sizeof(tmp))) > 0) {
        if (g_len + n > g_cap) { long nc = g_cap ? g_cap : 65536; while (nc < g_len + n) nc *= 2; g_buf = realloc(g_buf, nc); g_cap = nc; }
        memcpy(g_buf + g_len, tmp, n); g_len += n;
    }
}

static const char* g_prefix = "x";
static int g_suflen = 2, g_numeric = 0;

static void make_name(char* out, int idx) {
    int p = 0;
    for (const char* s = g_prefix; *s; s++) out[p++] = *s;
    char suf[32]; int base = g_numeric ? 10 : 26; char first = g_numeric ? '0' : 'a';
    for (int i = g_suflen - 1; i >= 0; i--) { suf[i] = (char)(first + idx % base); idx /= base; }
    for (int i = 0; i < g_suflen; i++) out[p++] = suf[i];
    out[p] = 0;
}

static int write_piece(long start, long end, int idx) {
    char name[288]; make_name(name, idx);
    long f = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f < 0) { printf("split: %s: cannot create\n", name); return -1; }
    long off = start;
    while (off < end) {
        int n = (int)write((int)f, g_buf + off, (int)(end - off));
        if (n <= 0) break;
        off += n;
    }
    close((int)f);
    return 0;
}

static int parse_size(const char* s, long* out) {
    long v = 0; int any = 0;
    const char* p = s;
    for (; *p >= '0' && *p <= '9'; p++) { v = v * 10 + (*p - '0'); any = 1; }
    if (!any) return -1;
    if (*p) {
        long mul = 0;
        if (*p == 'k' || *p == 'K') mul = 1024L;
        else if (*p == 'm' || *p == 'M') mul = 1024L * 1024;
        else if (*p == 'g' || *p == 'G') mul = 1024L * 1024 * 1024;
        else return -1;
        if (p[1]) return -1;
        v *= mul;
    }
    if (v < 1) return -1;
    *out = v; return 0;
}

static int parse_int(const char* s, int* out) {
    int v = 0, any = 0;
    for (const char* p = s; *p; p++) { if (*p < '0' || *p > '9') return -1; v = v * 10 + (*p - '0'); any = 1; }
    if (!any || v < 1) return -1;
    *out = v; return 0;
}

int main(int argc, char** argv) {
    int lines_per = 1000; long bytes_per = 0; int ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        if (!strcmp(argv[ai], "--")) { ai++; break; }
        for (const char* p = argv[ai] + 1; *p; p++) {
            if (*p == 'd') g_numeric = 1;
            else if (*p == 'l' || *p == 'b' || *p == 'a') {
                char opt = *p;
                const char* v = p[1] ? p + 1 : (ai + 1 < argc ? argv[++ai] : 0);
                if (!v) { printf("split: option -%c needs an argument\n", opt); return 1; }
                if (opt == 'l') { if (parse_int(v, &lines_per) < 0) { printf("split: invalid line count\n"); return 1; } bytes_per = 0; }
                else if (opt == 'b') { if (parse_size(v, &bytes_per) < 0) { printf("split: invalid byte count\n"); return 1; } }
                else { if (parse_int(v, &g_suflen) < 0) { printf("split: invalid suffix length\n"); return 1; } }
                break;
            } else { printf("split: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    const char* input = (ai < argc) ? argv[ai++] : "-";
    if (ai < argc) g_prefix = argv[ai++];

    if (!strcmp(input, "-")) append_fd(0);
    else { long f = open(input, O_RDONLY, 0); if (f < 0) { printf("split: %s: cannot open\n", input); return 1; } append_fd((int)f); close((int)f); }

    if (g_len == 0) return 0;   // GNU creates no files for empty input

    int idx = 0;
    if (bytes_per > 0) {
        for (long off = 0; off < g_len; off += bytes_per) {
            long e = off + bytes_per; if (e > g_len) e = g_len;
            if (write_piece(off, e, idx++) < 0) return 1;
        }
    } else {
        long start = 0, count = 0;
        for (long i = 0; i < g_len; i++) {
            if (g_buf[i] == '\n') { count++; if (count == lines_per) { if (write_piece(start, i + 1, idx++) < 0) return 1; start = i + 1; count = 0; } }
        }
        if (start < g_len) { if (write_piece(start, g_len, idx++) < 0) return 1; }
    }
    return 0;
}
