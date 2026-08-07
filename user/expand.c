#include "libc.h"

/* expand — convert tabs to spaces, like expand(1):
 *   expand [-i] [-t N | -t LIST] [FILE...]
 *     -i        convert only the tabs in each line's LEADING whitespace
 *     -t N      set tab stops every N columns (default 8)
 *     -t LIST   comma/blank-separated explicit stop columns; a single value means
 *               "every N", and past the last of several stops a tab is one column
 * A backspace backs the column up one, a newline resets it. With no FILE (or "-")
 * expands standard input. Output columns are counted in bytes (C locale). */

#define MAXSTOPS 256
static int stops[MAXSTOPS], nstops = 0;   /* explicit stop columns, used when nstops >= 2 */
static int tabsize = 8;                    /* uniform width, used when nstops < 2         */

/* The next tab stop strictly greater than col. */
static int next_stop(int col) {
    if (nstops >= 2) {
        for (int i = 0; i < nstops; i++) if (stops[i] > col) return stops[i];
        return col + 1;                              /* past the last explicit stop */
    }
    return col + (tabsize - col % tabsize);          /* uniform stops */
}

static char wbuf[8192]; static int wn = 0;
static void wflush(void) { if (wn) { write(1, wbuf, wn); wn = 0; } }
static void wput(char c) { if (wn == (int)sizeof(wbuf)) wflush(); wbuf[wn++] = c; }

static void expand_fd(int fd, int iflag) {
    int col = 0, leading = 1;
    unsigned char rb[4096];
    for (;;) {
        int n = (int)read(fd, rb, sizeof(rb));
        if (n <= 0) break;
        for (int k = 0; k < n; k++) {
            unsigned char c = rb[k];
            if (c == '\t') {
                if (iflag && !leading) { wput('\t'); col = next_stop(col); }   /* keep post-text tabs */
                else { int stop = next_stop(col); while (col < stop) { wput(' '); col++; } }
            } else if (c == '\b') {
                wput('\b'); if (col > 0) col--; leading = 0;
            } else if (c == '\n') {
                wput('\n'); col = 0; leading = 1;
            } else {
                if (c != ' ') leading = 0;             /* a space keeps the leading run alive */
                wput((char)c); col++;
            }
        }
    }
    wflush();
}

/* Parse a -t value (comma/blank list). Sets tabsize for a lone value, or stops[]
 * for several. Returns 0 ok, -1 on a bad character or an empty/zero size. */
static int parse_tablist(const char* v) {
    nstops = 0;
    int val = 0, have = 0;
    for (const char* p = v; ; p++) {
        if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); have = 1; }
        else if (*p == ',' || *p == ' ' || *p == '\0') {
            if (have) { if (nstops < MAXSTOPS) stops[nstops++] = val; }
            val = 0; have = 0;
            if (*p == '\0') break;
        } else return -1;
    }
    if (nstops == 0) return -1;
    if (nstops == 1) { tabsize = stops[0]; nstops = 0; if (tabsize < 1) return -1; }
    return 0;
}

int main(int argc, char** argv) {
    int iflag = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        if (argv[ai][1] == '-' && !argv[ai][2]) { ai++; break; }        /* "--" */
        for (const char* p = argv[ai] + 1; *p; p++) {
            if (*p == 'i') iflag = 1;
            else if (*p == 't') {
                const char* v = p[1] ? p + 1 : (ai + 1 < argc ? argv[++ai] : 0);
                if (!v || parse_tablist(v) < 0) { printf("expand: invalid tab size\n"); return 1; }
                break;                                                  /* value took the rest */
            } else { printf("expand: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (ai >= argc) { expand_fd(0, iflag); return 0; }
    int rc = 0;
    for (; ai < argc; ai++) {
        if (argv[ai][0] == '-' && !argv[ai][1]) { expand_fd(0, iflag); continue; }
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("expand: %s: cannot open\n", argv[ai]); rc = 1; continue; }
        expand_fd((int)f, iflag);
        close((int)f);
    }
    return rc;
}
