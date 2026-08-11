#include "libc.h"

/* column — line up the input into a table, like `column -t`. Each input line is split
 * into fields and every field is padded to the width of the widest entry in its column,
 * so the columns align. This implements TABLE mode only (the useful case); the option
 * `-t` is accepted and assumed, and the tty "fill into columns" mode is not implemented.
 *   -s SEP   split fields on any character in SEP (default: runs of whitespace)
 *   -o STR   put STR between columns (default: two spaces)
 *   -R LIST  right-justify the comma-separated, 1-based columns in LIST
 * FILEs are read in order (or standard input with none, or a "-" operand). */

#define MAXCOLS 256

/* ---- a growable byte buffer (used for both the slurped input and the output) ---- */
typedef struct { char* p; long len, cap; } buf_t;
static void bput(buf_t* b, const char* s, long n) {
    if (b->len + n > b->cap) {
        long nc = b->cap ? b->cap : 8192;
        while (nc < b->len + n) nc *= 2;
        b->p = (char*)realloc(b->p, nc);
        b->cap = nc;
    }
    for (long i = 0; i < n; i++) b->p[b->len + i] = s[i];
    b->len += n;
}
static void bspaces(buf_t* b, int n) { while (n-- > 0) bput(b, " ", 1); }

static int slurp_fd(buf_t* b, int fd) {
    char tmp[4096]; long n;
    while ((n = read(fd, tmp, sizeof(tmp))) > 0) bput(b, tmp, n);
    return (n < 0) ? -1 : 0;
}

/* Split line[0..len) into fields, filling fs[]/fl[] (start offset + length), at most
 * MAXCOLS. With sep, every character in sep delimits (no merging, so empty fields are
 * kept); without sep, each run of spaces/tabs is one delimiter (leading/trailing ignored). */
static int split_line(const char* line, int len, const char* sep, int* fs, int* fl) {
    int nf = 0;
    if (sep) {
        int start = 0;
        for (int i = 0; i <= len; i++) {
            int issep = (i < len) && strchr(sep, line[i]) != 0;
            if (i == len || issep) {
                if (nf < MAXCOLS) { fs[nf] = start; fl[nf] = i - start; nf++; }
                start = i + 1;
            }
        }
    } else {
        int i = 0;
        while (i < len) {
            while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
            if (i >= len) break;
            int s = i;
            while (i < len && line[i] != ' ' && line[i] != '\t') i++;
            if (nf < MAXCOLS) { fs[nf] = s; fl[nf] = i - s; nf++; }
        }
    }
    return nf;
}

int main(int argc, char** argv) {
    const char* sep = 0;
    const char* osep = "  ";
    static int right[MAXCOLS];
    int ai = 1;

    for (; ai < argc; ai++) {
        char* a = argv[ai];
        if (a[0] != '-' || a[1] == '\0') break;                 // FILE, or "-" for stdin
        if (a[1] == '-' && a[2] == '\0') { ai++; break; }       // "--"
        int done = 0;
        for (int k = 1; a[k] && !done; k++) {
            char c = a[k];
            if (c == 't') continue;                             // table mode: assumed
            if (c == 's' || c == 'o' || c == 'R') {
                const char* val;
                if (a[k + 1]) val = &a[k + 1];                  // -s,   (attached)
                else if (ai + 1 < argc) val = argv[++ai];       // -s ,  (separate)
                else { const char* e = "column: option requires an argument\n"; write(2, e, (int)strlen(e)); return 2; }
                if (c == 's') sep = val;
                else if (c == 'o') osep = val;
                else for (const char* q = val; *q; ) {          // -R 2,4  (1-based columns)
                    int v = 0; while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
                    if (v >= 1 && v <= MAXCOLS) right[v - 1] = 1;
                    if (*q == ',') q++; else if (*q) q++;
                }
                done = 1;                                       // the rest of this arg was the value
            } else { char m[64]; int o=0; const char* p="column: invalid option -- "; while(p[o]){m[o]=p[o];o++;} m[o++]=c; m[o++]='\n'; write(2,m,o); return 2; }
        }
    }

    buf_t in = {0,0,0};
    if (ai >= argc) { if (slurp_fd(&in, 0) < 0) return 1; }
    else for (; ai < argc; ai++) {
        const char* f = argv[ai];
        int fd;
        if (f[0] == '-' && f[1] == '\0') fd = 0;
        else { long g = open(f, O_RDONLY, 0);
            if (g < 0) { char m[300]; int o=0; const char* p="column: "; while(p[o]){m[o]=p[o];o++;} int q=0; while(f[q])m[o++]=f[q++]; const char* e=": No such file or directory\n"; q=0; while(e[q])m[o++]=e[q++]; write(2,m,o); return 1; }
            fd = (int)g; }
        slurp_fd(&in, fd);
        if (fd) close(fd);
    }

    int fs[MAXCOLS], fl[MAXCOLS];
    int ncols = 0;
    static int width[MAXCOLS];

    // Pass 1: how many columns, and how wide is each.
    for (long p = 0; p < in.len; ) {
        long ls = p; while (p < in.len && in.p[p] != '\n') p++;
        int nf = split_line(in.p + ls, (int)(p - ls), sep, fs, fl);
        if (nf > ncols) ncols = nf;
        for (int c = 0; c < nf; c++) if (fl[c] > width[c]) width[c] = fl[c];
        if (p < in.len) p++;                                    // step over the '\n'
    }

    // Pass 2: emit each line padded to the column widths.
    buf_t out = {0,0,0};
    for (long p = 0; p < in.len; ) {
        long ls = p; while (p < in.len && in.p[p] != '\n') p++;
        int nf = split_line(in.p + ls, (int)(p - ls), sep, fs, fl);
        for (int c = 0; c < ncols; c++) {
            const char* cell = (c < nf) ? in.p + ls + fs[c] : "";
            int clen = (c < nf) ? fl[c] : 0;
            int pad = width[c] - clen; if (pad < 0) pad = 0;
            if (c < ncols - 1) {
                if (right[c]) { bspaces(&out, pad); bput(&out, cell, clen); }
                else          { bput(&out, cell, clen); bspaces(&out, pad); }
                bput(&out, osep, (long)strlen(osep));
            } else {                                            // last column: no trailing pad
                if (right[c]) bspaces(&out, pad);
                bput(&out, cell, clen);
            }
        }
        bput(&out, "\n", 1);
        if (p < in.len) p++;
    }

    if (out.len) write(1, out.p, out.len);
    return 0;
}
