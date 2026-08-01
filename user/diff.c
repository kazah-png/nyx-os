#include "libc.h"

/* diff — compare two text files line by line and print their differences in the
 * classic "normal" diff format: hunk headers `LcR` (change), `LdR` (delete),
 * `LaR` (add), with file-1 lines prefixed `< ` and file-2 lines prefixed `> `,
 * a `---` separating the two sides of a change. Uses a longest-common-subsequence
 * of lines so only genuine edits are reported. Bounded to MAX_LINES lines and a
 * fixed byte buffer per file. Exit status: 0 if identical, 1 if they differ, 2 on
 * error (like GNU diff). */

#define MAX_LINES 200
#define BUF_CAP   32768

static char  buf1[BUF_CAP], buf2[BUF_CAP];
static int   off1[MAX_LINES], len1[MAX_LINES];
static int   off2[MAX_LINES], len2[MAX_LINES];
static short lcs[MAX_LINES + 1][MAX_LINES + 1];
static char  ops[2 * MAX_LINES];   /* forward edit script: 'K' keep, 'D' delete, 'A' add */

/* Read a whole file into `buf`, split into line records (content without '\n').
 * Returns the line count, or -1 if the file can't be opened. */
static int load(const char* path, char* buf, int* off, int* len) {
    long f = open(path, O_RDONLY, 0);
    if (f < 0) { printf("diff: %s: not found\n", path); return -1; }
    int total = 0, n;
    while (total < BUF_CAP && (n = (int)read((int)f, buf + total, BUF_CAP - total)) > 0) total += n;
    close((int)f);
    int nl = 0, i = 0;
    while (i < total && nl < MAX_LINES) {
        int s = i;
        while (i < total && buf[i] != '\n') i++;
        off[nl] = s; len[nl] = i - s; nl++;
        if (i < total) i++;
    }
    return nl;
}

static int eq(const char* a, int la, const char* b, int lb) {
    if (la != lb) return 0;
    for (int i = 0; i < la; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void put_line(char pfx, const char* base, int off, int len) {
    char p[2] = { pfx, ' ' };
    write(1, p, 2);
    write(1, base + off, len);
    write(1, "\n", 1);
}

static void put_range(int lo, int hi) {   /* 1-based inclusive */
    char b[32];
    int n = (lo == hi) ? snprintf(b, sizeof(b), "%d", lo) : snprintf(b, sizeof(b), "%d,%d", lo, hi);
    write(1, b, n);
}

static void put_int(int v) { char b[16]; write(1, b, snprintf(b, sizeof(b), "%d", v)); }

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: diff FILE1 FILE2\n"); return 2; }
    int n1 = load(argv[1], buf1, off1, len1);
    int n2 = load(argv[2], buf2, off2, len2);
    if (n1 < 0 || n2 < 0) return 2;

    /* LCS length table over lines: lcs[i][j] = LCS(file1[i..], file2[j..]). */
    for (int i = n1 - 1; i >= 0; i--)
        for (int j = n2 - 1; j >= 0; j--) {
            if (eq(buf1 + off1[i], len1[i], buf2 + off2[j], len2[j]))
                lcs[i][j] = (short)(lcs[i + 1][j + 1] + 1);
            else
                lcs[i][j] = (lcs[i + 1][j] >= lcs[i][j + 1]) ? lcs[i + 1][j] : lcs[i][j + 1];
        }

    /* Follow an optimal path forward, recording keep/delete/add. */
    int i = 0, j = 0, no = 0;
    while (i < n1 || j < n2) {
        if (i < n1 && j < n2 && eq(buf1 + off1[i], len1[i], buf2 + off2[j], len2[j])) { ops[no++] = 'K'; i++; j++; }
        else if (j < n2 && (i == n1 || lcs[i][j + 1] >= lcs[i + 1][j]))               { ops[no++] = 'A'; j++; }
        else                                                                          { ops[no++] = 'D'; i++; }
    }

    /* Group the script into hunks (runs of D/A bounded by K) and print each. */
    int changed = 0, k = 0, l1 = 0, l2 = 0;
    while (k < no) {
        if (ops[k] == 'K') { l1++; l2++; k++; continue; }
        int d_start = l1 + 1, a_start = l2 + 1, nd = 0, na = 0;
        while (k < no && ops[k] != 'K') { if (ops[k] == 'D') { nd++; l1++; } else { na++; l2++; } k++; }
        int d_end = d_start + nd - 1, a_end = a_start + na - 1;
        changed = 1;

        if (nd > 0 && na > 0) {                     /* change */
            put_range(d_start, d_end); write(1, "c", 1); put_range(a_start, a_end); write(1, "\n", 1);
            for (int x = d_start; x <= d_end; x++) put_line('<', buf1, off1[x - 1], len1[x - 1]);
            write(1, "---\n", 4);
            for (int x = a_start; x <= a_end; x++) put_line('>', buf2, off2[x - 1], len2[x - 1]);
        } else if (nd > 0) {                         /* delete */
            put_range(d_start, d_end); write(1, "d", 1); put_int(a_start - 1); write(1, "\n", 1);
            for (int x = d_start; x <= d_end; x++) put_line('<', buf1, off1[x - 1], len1[x - 1]);
        } else {                                     /* add */
            put_int(d_start - 1); write(1, "a", 1); put_range(a_start, a_end); write(1, "\n", 1);
            for (int x = a_start; x <= a_end; x++) put_line('>', buf2, off2[x - 1], len2[x - 1]);
        }
    }
    return changed ? 1 : 0;
}
