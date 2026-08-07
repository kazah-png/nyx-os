#include "libc.h"

/* paste — merge lines of files, like paste(1):
 *   paste [-s] [-d LIST] [FILE...]
 * Default (parallel): the Rth output line joins the Rth line of every FILE with a
 * TAB (or, with -d, the delimiters of LIST used in turn). A file that runs out of
 * lines contributes an empty field; output continues until the longest file ends.
 * -s (serial): each FILE is collapsed onto ONE line, its own lines joined by the
 * delimiters. -d LIST sets the delimiter list; the escapes \n \t \\ and \0 (the
 * empty delimiter) are recognised and the list is cycled across field boundaries.
 * A FILE of '-' (or none) is standard input; several '-' columns share stdin and
 * pull lines in turn (the `paste - -` reshaping trick). Bounded to PASTE_MAXF
 * columns, STORE_CAP bytes and MAXREC lines per source. */

#define PASTE_MAXF 32
#define STORE_CAP  (128 * 1024)
#define MAXREC     8000
#define MAXD       128

/* ---- delimiter list parsed from -d (default a single TAB) ---- */
static char dch[MAXD];       /* the delimiter byte (unused when dempty[i]) */
static char dempty[MAXD];    /* 1 => this delimiter emits nothing (from \0) */
static int  dlen = 0;

static void parse_delims(const char* s) {
    dlen = 0;
    while (*s && dlen < MAXD) {
        if (*s == '\\' && s[1]) {
            s++;
            char c = *s++;
            switch (c) {
                case 'n': dch[dlen] = '\n'; dempty[dlen] = 0; dlen++; break;
                case 't': dch[dlen] = '\t'; dempty[dlen] = 0; dlen++; break;
                case '\\': dch[dlen] = '\\'; dempty[dlen] = 0; dlen++; break;
                case '0': dch[dlen] = 0;    dempty[dlen] = 1; dlen++; break;
                default:  dch[dlen] = c;    dempty[dlen] = 0; dlen++; break;
            }
        } else {
            dch[dlen] = *s++; dempty[dlen] = 0; dlen++;
        }
    }
    if (dlen == 0) { dch[0] = 0; dempty[0] = 1; dlen = 1; }   /* -d '' => no separator */
}

/* ---- buffered stdout (one syscall per 8 KiB, not per byte) ---- */
static char wbuf[8192];
static int  wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(const char* s, int n) {
    for (int k = 0; k < n; k++) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = s[k]; }
}
static void wsep(int k) { int i = k % dlen; if (!dempty[i]) wput(&dch[i], 1); }

/* ---- named-file columns share one store; each column owns a slice of rec[] ---- */
static char store[STORE_CAP]; static int store_used = 0;
static int  roff[MAXREC], rlen[MAXREC]; static int rec_used = 0;
static int  fstart[PASTE_MAXF], fcount[PASTE_MAXF];
static char is_stdin[PASTE_MAXF];

/* ---- stdin is read once into its own pool, shared by every '-' column ---- */
static char sstore[STORE_CAP]; static int sstore_used = 0;
static int  soff[MAXREC], slen[MAXREC]; static int scount = 0, sidx = 0;
static int  stdin_loaded = 0;

/* Split [from, to) of buf into line records (content without the '\n'); a final
 * chunk with no trailing newline still counts as a line. Returns lines added. */
static int split_lines(const char* buf, int from, int to, int* off, int* len, int* used, int cap) {
    int added = 0, i = from;
    while (i < to && *used < cap) {
        int s = i;
        while (i < to && buf[i] != '\n') i++;
        off[*used] = s; len[*used] = i - s; (*used)++; added++;
        if (i < to) i++;
    }
    return added;
}

static int load_file(const char* path, int col) {
    long f = open(path, O_RDONLY, 0);
    if (f < 0) { printf("paste: %s: cannot open\n", path); return -1; }
    int fd = (int)f, start = store_used, n;
    while (store_used < STORE_CAP && (n = (int)read(fd, store + store_used, STORE_CAP - store_used)) > 0)
        store_used += n;
    close(fd);
    fstart[col] = rec_used;
    split_lines(store, start, store_used, roff, rlen, &rec_used, MAXREC);
    fcount[col] = rec_used - fstart[col];
    return 0;
}

static void load_stdin(void) {
    if (stdin_loaded) return;
    stdin_loaded = 1;
    int n;
    while (sstore_used < STORE_CAP && (n = (int)read(0, sstore + sstore_used, STORE_CAP - sstore_used)) > 0)
        sstore_used += n;
    split_lines(sstore, 0, sstore_used, soff, slen, &scount, MAXREC);
}

int main(int argc, char** argv) {
    int serial = 0, ai = 1, have_d = 0;

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == '-' && !a[2]) { ai++; break; }               /* "--" ends options */
        for (int k = 1; a[k]; k++) {
            if (a[k] == 's') serial = 1;
            else if (a[k] == 'd') {
                const char* list;
                if (a[k + 1]) list = &a[k + 1];                  /* -dLIST */
                else if (ai + 1 < argc) list = argv[++ai];       /* -d LIST */
                else { printf("paste: option requires an argument -- 'd'\n"); return 2; }
                have_d = 1; parse_delims(list);
                break;                                           /* LIST consumed the rest of this arg */
            } else { printf("paste: invalid option -- '%c'\n", a[k]); return 2; }
        }
    }
    if (!have_d) { dch[0] = '\t'; dempty[0] = 0; dlen = 1; }      /* default delimiter is TAB */

    /* Collect the column operands; no operands means a single stdin column. */
    int ncol = 0;
    if (ai >= argc) { is_stdin[ncol] = 1; ncol++; }
    else {
        for (; ai < argc && ncol < PASTE_MAXF; ai++) {
            const char* a = argv[ai];
            if (a[0] == '-' && !a[1]) { is_stdin[ncol] = 1; fcount[ncol] = 0; ncol++; }
            else { is_stdin[ncol] = 0; if (load_file(a, ncol) < 0) return 1; ncol++; }
        }
    }
    for (int c = 0; c < ncol; c++) if (is_stdin[c]) { load_stdin(); break; }

    if (serial) {
        for (int c = 0; c < ncol; c++) {
            if (is_stdin[c]) {
                for (int j = 0; sidx < scount; sidx++, j++) {
                    if (j) wsep(j - 1);
                    wput(sstore + soff[sidx], slen[sidx]);
                }
            } else {
                for (int j = 0; j < fcount[c]; j++) {
                    if (j) wsep(j - 1);
                    wput(store + roff[fstart[c] + j], rlen[fstart[c] + j]);
                }
            }
            wput("\n", 1);
        }
        wflush();
        return 0;
    }

    /* Parallel: one row per line index, up to the longest file (stdin drains its pool). */
    int filerows = 0;
    for (int c = 0; c < ncol; c++) if (!is_stdin[c] && fcount[c] > filerows) filerows = fcount[c];
    for (int r = 0; r < filerows || sidx < scount; r++) {
        for (int c = 0; c < ncol; c++) {
            if (c) wsep(c - 1);
            if (is_stdin[c]) {
                if (sidx < scount) { wput(sstore + soff[sidx], slen[sidx]); sidx++; }
            } else if (r < fcount[c]) {
                wput(store + roff[fstart[c] + r], rlen[fstart[c] + r]);
            }
        }
        wput("\n", 1);
    }
    wflush();
    return 0;
}
