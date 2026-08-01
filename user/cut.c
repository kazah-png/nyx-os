#include "libc.h"

/* cut — extract selected fields or characters from each line, like GNU cut.
 *   cut -f LIST [-d DELIM] [file]   select DELIM-separated fields (DELIM default TAB)
 *   cut -c LIST [file]              select character positions
 * LIST is a comma-separated set of N, N-M, N- (N to end of line), or -M (start
 * to M); positions are 1-based. Reads a file argument or, with none, stdin.
 * Like GNU cut, selected pieces are emitted in increasing position order, and in
 * -f mode a line that contains no delimiter is passed through unchanged. */

typedef struct { int lo, hi; } range_t;   /* hi == 0 means open-ended (to end) */
static range_t g_ranges[32];
static int g_nranges = 0;

/* Parse a cut LIST into g_ranges[]. Returns 1 on success, 0 on a malformed list. */
static int parse_list(const char* s) {
    g_nranges = 0;
    while (*s) {
        int lo = 0, hi = 0, have_lo = 0;
        while (*s >= '0' && *s <= '9') { lo = lo * 10 + (*s - '0'); s++; have_lo = 1; }
        if (*s == '-') {
            s++;
            if (*s >= '0' && *s <= '9') { while (*s >= '0' && *s <= '9') { hi = hi * 10 + (*s - '0'); s++; } }
            else hi = 0;                 /* "N-" : open-ended */
            if (!have_lo) lo = 1;        /* "-M" : from the start */
        } else {
            hi = lo;                     /* single "N" */
        }
        if (!have_lo && hi == 0) return 0;   /* empty / bare '-' */
        if (lo < 1) lo = 1;
        if (g_nranges < 32) { g_ranges[g_nranges].lo = lo; g_ranges[g_nranges].hi = hi; g_nranges++; }
        if (*s == ',') s++;
        else if (*s) return 0;           /* junk after a range */
    }
    return g_nranges > 0;
}

static int selected(int idx) {           /* idx is 1-based */
    for (int i = 0; i < g_nranges; i++) {
        int lo = g_ranges[i].lo, hi = g_ranges[i].hi;
        if (idx >= lo && (hi == 0 || idx <= hi)) return 1;
    }
    return 0;
}

/* A small buffered line reader over one fd (inputs here are modest). */
static char rbuf[1024];
static int  rlen = 0, rpos = 0;

static int refill(int fd) { rpos = 0; rlen = (int)read(fd, rbuf, sizeof(rbuf)); return rlen; }

static int read_line(int fd, char* line, int cap) {
    int len = 0;
    for (;;) {
        if (rpos >= rlen) { if (refill(fd) <= 0) break; }
        char ch = rbuf[rpos++];
        if (ch == '\n') return len;
        if (len < cap - 1) line[len++] = ch;
    }
    return len > 0 ? len : -1;
}

int main(int argc, char** argv) {
    int mode = 0;             /* 'f' or 'c' */
    char delim = '\t';
    const char* list = 0;
    int ai = 1;

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        const char* a = argv[ai];
        if (a[1] == 'f')      { mode = 'f'; list = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0); }
        else if (a[1] == 'c') { mode = 'c'; list = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0); }
        else if (a[1] == 'd') { const char* d = a[2] ? a + 2 : (ai + 1 < argc ? argv[++ai] : 0); if (d) delim = d[0]; }
        else { printf("cut: invalid option %s\n", a); return 1; }
    }
    if (!mode || !list)   { printf("usage: cut -f LIST [-d C] [file] | cut -c LIST [file]\n"); return 1; }
    if (!parse_list(list)) { printf("cut: invalid list '%s'\n", list); return 1; }

    int fd = 0;   /* stdin by default */
    if (ai < argc) {
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("cut: %s: not found\n", argv[ai]); return 1; }
        fd = (int)f;
    }

    char line[1024], out[1024];
    for (;;) {
        int len = read_line(fd, line, sizeof(line));
        if (len < 0) break;
        int olen = 0;

        if (mode == 'c') {
            for (int i = 0; i < len; i++)
                if (selected(i + 1) && olen < (int)sizeof(out)) out[olen++] = line[i];
        } else {   /* -f */
            int has_delim = 0;
            for (int i = 0; i < len; i++) if (line[i] == delim) { has_delim = 1; break; }
            if (!has_delim) { write(1, line, len); write(1, "\n", 1); continue; }
            int field = 1, started = 0, seg = 0;
            for (int i = 0; i <= len; i++) {
                if (i == len || line[i] == delim) {
                    if (selected(field)) {
                        if (started && olen < (int)sizeof(out)) out[olen++] = delim;
                        for (int j = seg; j < i && olen < (int)sizeof(out); j++) out[olen++] = line[j];
                        started = 1;
                    }
                    field++;
                    seg = i + 1;
                }
            }
        }

        write(1, out, olen);
        write(1, "\n", 1);
    }

    if (fd != 0) close(fd);
    return 0;
}
