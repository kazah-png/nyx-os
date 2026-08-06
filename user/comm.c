#include "libc.h"

/* comm — compare two SORTED files line by line and print three columns, like comm(1):
 *   comm [-123] FILE1 FILE2
 * Column 1 = lines only in FILE1, column 2 = lines only in FILE2 (indented one tab),
 * column 3 = lines in both (indented two tabs). -1 / -2 / -3 suppress that column, and
 * the columns that remain shift left — a printed column's indent is the number of
 * non-suppressed columns to its left. Either FILE may be '-' for standard input. The
 * inputs are assumed sorted in byte order (C locale), the same order this merges in
 * (feed it `sort`'s output); each file is bounded to MAX_LINES lines / BUF_CAP bytes. */

#define MAX_LINES 4000
#define BUF_CAP   65536

static char buf1[BUF_CAP], buf2[BUF_CAP];
static int  off1[MAX_LINES], len1[MAX_LINES];
static int  off2[MAX_LINES], len2[MAX_LINES];

/* Read a whole file (or stdin for "-") into buf, split into line records (content
 * without the '\n'). Returns the line count, or -1 if the file can't be opened. */
static int load(const char* path, char* buf, int* off, int* len) {
    int fd;
    if (path[0] == '-' && path[1] == '\0') fd = 0;
    else {
        long f = open(path, O_RDONLY, 0);
        if (f < 0) { printf("comm: %s: not found\n", path); return -1; }
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

/* Byte-wise lexicographic compare (C locale), matching sort/strcmp order: a shared
 * prefix makes the shorter line sort first. */
static int cmpln(const char* a, int la, const char* b, int lb) {
    int m = la < lb ? la : lb;
    for (int i = 0; i < m; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (la != lb) return la < lb ? -1 : 1;
    return 0;
}

/* Buffered stdout so we don't pay a syscall per byte. */
static char wbuf[4096];
static int  wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(const char* s, int n) { for (int k = 0; k < n; k++) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = s[k]; } }

int main(int argc, char** argv) {
    int s1 = 0, s2 = 0, s3 = 0, ai = 1;
    for (; ai < argc; ai++) {
        const char* a = argv[ai];
        if (a[0] != '-' || a[1] == '\0') break;          // a bare "-" or a plain name is an operand
        int ok = 1;
        for (int k = 1; a[k]; k++) {
            if      (a[k] == '1') s1 = 1;
            else if (a[k] == '2') s2 = 1;
            else if (a[k] == '3') s3 = 1;
            else { ok = 0; break; }
        }
        if (!ok) { printf("comm: invalid option %s\n", a); return 2; }
    }
    if (argc - ai != 2) { printf("comm: usage: comm [-123] FILE1 FILE2\n"); return 2; }

    int n1 = load(argv[ai],     buf1, off1, len1);
    int n2 = load(argv[ai + 1], buf2, off2, len2);
    if (n1 < 0 || n2 < 0) return 1;

    // A printed column's indent = the number of non-suppressed columns to its left.
    int t2 = s1 ? 0 : 1;
    int t3 = (s1 ? 0 : 1) + (s2 ? 0 : 1);
    static const char TABS[] = "\t\t";
    #define EMIT(tabs, base, o, l) do { wput(TABS, (tabs)); wput((base) + (o), (l)); wput("\n", 1); } while (0)

    int i = 0, j = 0;
    while (i < n1 && j < n2) {
        int c = cmpln(buf1 + off1[i], len1[i], buf2 + off2[j], len2[j]);
        if      (c == 0) { if (!s3) EMIT(t3, buf1, off1[i], len1[i]); i++; j++; }
        else if (c < 0)  { if (!s1) EMIT(0,  buf1, off1[i], len1[i]); i++; }
        else             { if (!s2) EMIT(t2, buf2, off2[j], len2[j]); j++; }
    }
    while (i < n1) { if (!s1) EMIT(0,  buf1, off1[i], len1[i]); i++; }
    while (j < n2) { if (!s2) EMIT(t2, buf2, off2[j], len2[j]); j++; }
    wflush();
    return 0;
}
