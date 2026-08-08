#include "libc.h"

/* unexpand — convert blanks to tabs, the inverse of expand(1):
 *   unexpand [-a] [-t N] [FILE...]
 *     (default)  convert only each line's LEADING run of blanks
 *     -a         convert every run of two or more blanks, not just leading ones
 *     -t N       set tab stops every N columns (default 8); implies -a
 * A run of blanks is rewritten to the fewest bytes: a tab is emitted only when it
 * spans two or more columns to the next tab stop (so a lone space is never turned
 * into a tab). With no FILE (or "-") it reads standard input. */

static int tabsize = 8;
static int aflag = 0;

static char wbuf[8192]; static int wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(char c) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = c; }

static int next_stop(int col) { return col + (tabsize - col % tabsize); }

/* Emit a run of blanks spanning columns [s, e): a lone blank is echoed unchanged
 * (never worth a tab); a run of two or more is rewritten to a tab at every tab
 * stop it reaches, plus spaces for the remainder. */
static void emit_run(int s, int e, int nchars, unsigned char first) {
    if (nchars <= 1) { if (nchars == 1) wput((char)first); return; }
    int col = s;
    while (col < e) {
        int stop = next_stop(col);
        if (stop <= e) { wput('\t'); col = stop; }
        else { wput(' '); col++; }
    }
}

static void unexpand_fd(int fd) {
    int col = 0, convert = 1, in_run = 0, run_start = 0, run_nchars = 0;
    unsigned char run_first = 0, rb[4096]; int n;
    while ((n = (int)read(fd, rb, sizeof(rb))) > 0) {
        for (int k = 0; k < n; k++) {
            unsigned char c = rb[k];
            if ((c == ' ' || c == '\t') && convert) {
                if (!in_run) { run_start = col; run_nchars = 0; run_first = c; in_run = 1; }
                run_nchars++;
                col = (c == '\t') ? next_stop(col) : col + 1;
            } else {
                if (in_run) { emit_run(run_start, col, run_nchars, run_first); in_run = 0; }
                if (c == '\n') { wput('\n'); col = 0; convert = 1; }
                else if (c == '\b') { wput('\b'); if (col > 0) col--; if (!aflag) convert = 0; }
                else if (c == '\t') { wput('\t'); col = next_stop(col); if (!aflag) convert = 0; }
                else if (c == ' ')  { wput(' '); col++; if (!aflag) convert = 0; }
                else { wput((char)c); col++; if (!aflag) convert = 0; }
            }
        }
    }
    if (in_run) emit_run(run_start, col, run_nchars, run_first);   /* trailing blanks, no final newline */
    wflush();
}

static int parse_tabsize(const char* v) {
    int t = 0, any = 0;
    for (const char* p = v; *p; p++) { if (*p < '0' || *p > '9') return -1; t = t * 10 + (*p - '0'); any = 1; }
    if (!any || t < 1) return -1;
    tabsize = t; return 0;
}

int main(int argc, char** argv) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (!strcmp(argv[i], "--")) { i++; break; }
        if (!strcmp(argv[i], "--all")) { aflag = 1; continue; }
        if (!strcmp(argv[i], "--first-only")) { aflag = 0; continue; }
        for (const char* p = argv[i] + 1; *p; p++) {
            if (*p == 'a') aflag = 1;
            else if (*p == 't') {
                const char* v = p[1] ? p + 1 : (i + 1 < argc ? argv[++i] : 0);
                if (!v || parse_tabsize(v) < 0) { printf("unexpand: invalid tab size\n"); return 1; }
                aflag = 1;                       /* -t implies -a, matching GNU */
                break;
            } else { printf("unexpand: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (i >= argc) { unexpand_fd(0); return 0; }
    int rc = 0;
    for (; i < argc; i++) {
        if (argv[i][0] == '-' && !argv[i][1]) { unexpand_fd(0); continue; }
        long f = open(argv[i], O_RDONLY, 0);
        if (f < 0) { printf("unexpand: %s: cannot open\n", argv[i]); rc = 1; continue; }
        unexpand_fd((int)f);
        close((int)f);
    }
    return rc;
}
