#include "libc.h"

/* join — relational join of two SORTED files on a common field, like join(1):
 *   join [-1 F] [-2 F] [-j F] [-t C] [-a N] [-v N] FILE1 FILE2
 * For each pair of input lines that share a join field, print the join field, then the
 * remaining fields of FILE1, then the remaining fields of FILE2. Fields are separated by
 * runs of whitespace on input and a single space on output; -t C makes C the one-character
 * separator for both. -1/-2 set the join field (1-based) in each file (-j sets both).
 * Lines with equal join fields are cross-joined (every pair). -a N also prints unpairable
 * lines from file N; -v N prints ONLY unpairable lines from file N. Either FILE may be '-'
 * for standard input. Inputs must be sorted on the join field (C locale), the order this
 * merges in; bounded to MAX_LINES lines / BUF_CAP bytes each. -o/-e are not implemented. */

#define MAX_LINES 4000
#define BUF_CAP   65536
#define MAXF      64

static char buf1[BUF_CAP], buf2[BUF_CAP];
static int  off1[MAX_LINES], len1[MAX_LINES];
static int  off2[MAX_LINES], len2[MAX_LINES];

static int load(const char* path, char* buf, int* off, int* len) {
    int fd;
    if (path[0] == '-' && path[1] == '\0') fd = 0;
    else {
        long f = open(path, O_RDONLY, 0);
        if (f < 0) { printf("join: %s: not found\n", path); return -1; }
        fd = (int)f;
    }
    int total = 0, n;
    while (total < BUF_CAP && (n = (int)read(fd, buf + total, BUF_CAP - total)) > 0) total += n;
    if (fd != 0) close(fd);
    int nl = 0, i = 0;
    while (i < total && nl < MAX_LINES) {
        int s = i;
        while (i < total && buf[i] != '\n') i++;
        off[nl] = s; len[nl] = i - s; nl++;
        if (i < total) i++;
    }
    return nl;
}

/* Split line s[0..n) into fields. sep==0: skip leading blanks, fields are maximal
 * non-blank runs (runs of blanks collapse). sep!=0: each sep byte separates a field
 * (empty fields kept, no collapsing). Returns the field count (capped at MAXF). */
static int split(const char* s, int n, char sep, int* foff, int* flen) {
    int nf = 0;
    if (sep == 0) {
        int i = 0;
        while (i < n && nf < MAXF) {
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            if (i >= n) break;
            int st = i;
            while (i < n && s[i] != ' ' && s[i] != '\t') i++;
            foff[nf] = st; flen[nf] = i - st; nf++;
        }
    } else {
        int i = 0, st = 0;
        for (;;) {
            if (i == n || s[i] == sep) {
                if (nf < MAXF) { foff[nf] = st; flen[nf] = i - st; nf++; }
                if (i == n) break;
                i++; st = i;
            } else i++;
        }
    }
    return nf;
}

static int cmpln(const char* a, int la, const char* b, int lb) {
    int m = la < lb ? la : lb;
    for (int i = 0; i < m; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (la != lb) return la < lb ? -1 : 1;
    return 0;
}

/* Buffered stdout. */
static char wbuf[8192];
static int  wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(const char* s, int n) { for (int k = 0; k < n; k++) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = s[k]; } }
static void wc1(char c) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = c; }

static char g_sep = 0;        /* output separator char, or 0 -> single space */
static void osep(void) { wc1(g_sep ? g_sep : ' '); }

/* The join key of a line = its kf-th field (0-based), or the empty string if absent. */
static void keyof(const char* base, int off, int len, char sep, int kf, const char** kp, int* kl) {
    int fo[MAXF], fl[MAXF];
    int nf = split(base + off, len, sep, fo, fl);
    if (kf < nf) { *kp = base + off + fo[kf]; *kl = fl[kf]; }
    else         { *kp = base + off;          *kl = 0; }
}

/* Print a line's fields with the join field first, then every other field in order. */
static void emit_reordered(const char* base, int off, int len, char sep, int kf) {
    int fo[MAXF], fl[MAXF];
    int nf = split(base + off, len, sep, fo, fl);
    if (kf < nf) wput(base + off + fo[kf], fl[kf]);   /* join field first (else empty) */
    for (int f = 0; f < nf; f++) {
        if (f == kf) continue;
        osep();
        wput(base + off + fo[f], fl[f]);
    }
}

/* Print a joined pair: join field, then file1's remaining fields, then file2's. */
static void emit_join(int i, int j, char sep, int kf1, int kf2) {
    int fo1[MAXF], fl1[MAXF], fo2[MAXF], fl2[MAXF];
    int nf1 = split(buf1 + off1[i], len1[i], sep, fo1, fl1);
    int nf2 = split(buf2 + off2[j], len2[j], sep, fo2, fl2);
    if (kf1 < nf1)      wput(buf1 + off1[i] + fo1[kf1], fl1[kf1]);
    else if (kf2 < nf2) wput(buf2 + off2[j] + fo2[kf2], fl2[kf2]);
    for (int f = 0; f < nf1; f++) { if (f == kf1) continue; osep(); wput(buf1 + off1[i] + fo1[f], fl1[f]); }
    for (int f = 0; f < nf2; f++) { if (f == kf2) continue; osep(); wput(buf2 + off2[j] + fo2[f], fl2[f]); }
}

static int parse_int(const char* s) { int v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }

int main(int argc, char** argv) {
    int j1 = 1, j2 = 1;                 /* 1-based join fields */
    int a1 = 0, a2 = 0, v1 = 0, v2 = 0;
    const char* files[2]; int nfiles = 0;

    for (int ai = 1; ai < argc; ai++) {
        const char* a = argv[ai];
        if (a[0] == '-' && a[1] != '\0') {
            char opt = a[1];
            const char* val = a[2] ? a + 2 : 0;                 /* attached value, if any */
            #define NEEDVAL() (val ? val : (++ai < argc ? argv[ai] : 0))
            if      (opt == '1') { const char* v = NEEDVAL(); if (!v) { printf("join: -1 needs a field\n"); return 2; } j1 = parse_int(v); }
            else if (opt == '2') { const char* v = NEEDVAL(); if (!v) { printf("join: -2 needs a field\n"); return 2; } j2 = parse_int(v); }
            else if (opt == 'j') { const char* v = NEEDVAL(); if (!v) { printf("join: -j needs a field\n"); return 2; } j1 = j2 = parse_int(v); }
            else if (opt == 't') { const char* v = NEEDVAL(); if (!v) { printf("join: -t needs a char\n"); return 2; } g_sep = v[0]; }
            else if (opt == 'a') { const char* v = NEEDVAL(); int n = v ? parse_int(v) : 0; if (n == 1) a1 = 1; else if (n == 2) a2 = 1; else { printf("join: -a needs 1 or 2\n"); return 2; } }
            else if (opt == 'v') { const char* v = NEEDVAL(); int n = v ? parse_int(v) : 0; if (n == 1) v1 = 1; else if (n == 2) v2 = 1; else { printf("join: -v needs 1 or 2\n"); return 2; } }
            else { printf("join: invalid option -%c\n", opt); return 2; }
        } else {
            if (nfiles < 2) files[nfiles++] = a;
        }
    }
    if (nfiles != 2) { printf("join: usage: join [-1 F] [-2 F] [-t C] [-a N] [-v N] FILE1 FILE2\n"); return 2; }
    if (j1 < 1) j1 = 1;
    if (j2 < 1) j2 = 1;

    int n1 = load(files[0], buf1, off1, len1);
    int n2 = load(files[1], buf2, off2, len2);
    if (n1 < 0 || n2 < 0) return 1;

    int kf1 = j1 - 1, kf2 = j2 - 1;
    int paired = (!v1 && !v2);          /* any -v suppresses the joined output */
    int un1 = a1 || v1, un2 = a2 || v2;  /* print unpairable lines from file 1 / 2 */

    int i = 0, j = 0;
    while (i < n1 && j < n2) {
        const char *k1, *k2; int l1, l2;
        keyof(buf1, off1[i], len1[i], g_sep, kf1, &k1, &l1);
        keyof(buf2, off2[j], len2[j], g_sep, kf2, &k2, &l2);
        int c = cmpln(k1, l1, k2, l2);
        if (c < 0) { if (un1) { emit_reordered(buf1, off1[i], len1[i], g_sep, kf1); wc1('\n'); } i++; }
        else if (c > 0) { if (un2) { emit_reordered(buf2, off2[j], len2[j], g_sep, kf2); wc1('\n'); } j++; }
        else {
            int i2 = i, j2 = j;               /* group extents with this key */
            while (i2 < n1) { const char* kk; int ll; keyof(buf1, off1[i2], len1[i2], g_sep, kf1, &kk, &ll); if (cmpln(kk, ll, k1, l1)) break; i2++; }
            while (j2 < n2) { const char* kk; int ll; keyof(buf2, off2[j2], len2[j2], g_sep, kf2, &kk, &ll); if (cmpln(kk, ll, k2, l2)) break; j2++; }
            if (paired)
                for (int a = i; a < i2; a++)
                    for (int b = j; b < j2; b++) { emit_join(a, b, g_sep, kf1, kf2); wc1('\n'); }
            i = i2; j = j2;
        }
    }
    while (i < n1) { if (un1) { emit_reordered(buf1, off1[i], len1[i], g_sep, kf1); wc1('\n'); } i++; }
    while (j < n2) { if (un2) { emit_reordered(buf2, off2[j], len2[j], g_sep, kf2); wc1('\n'); } j++; }
    wflush();
    return 0;
}
