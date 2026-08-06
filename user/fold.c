#include "libc.h"

/* fold — wrap each input line to a given width, like GNU fold.
 *   fold [-b] [-s] [-w WIDTH] [FILE...]
 *     -w WIDTH  wrap at WIDTH columns (default 80); accepts -w N / -wN
 *     -b        count BYTES instead of columns (tabs/backspace/CR count as 1)
 *     -s        break at the last blank within WIDTH, so words stay whole
 * In column mode a tab advances to the next multiple of 8, a backspace backs up
 * one column, and a carriage return resets to column 0 — matching GNU fold. With
 * no FILE (or "-") it folds stdin. Wrapping never drops input; it only inserts
 * newlines, and a line already <= WIDTH passes through untouched. */

#define LINE_MAX 16384

static unsigned char line_out[LINE_MAX];

static int is_blank(unsigned char c) { return c == ' ' || c == '\t'; }

/* Column reached after emitting byte c, starting from `col`. */
static int adjust_column(int col, unsigned char c, int count_bytes) {
    if (count_bytes) return col + 1;
    if (c == '\b') { if (col > 0) col--; }
    else if (c == '\r') col = 0;
    else if (c == '\t') col += 8 - (col % 8);
    else col++;
    return col;
}

/* Fold one fd to stdout. State is local, so each FILE folds independently. */
static void fold_fd(int fd, int width, int bflag, int sflag) {
    int offset = 0, column = 0;
    unsigned char rb[4096];
    for (;;) {
        int n = (int)read(fd, rb, sizeof(rb));
        if (n <= 0) break;
        for (int k = 0; k < n; k++) {
            unsigned char c = rb[k];
            if (c == '\n') {                       // hard line break: flush as-is
                if (offset) write(1, line_out, offset);
                write(1, "\n", 1);
                offset = 0; column = 0;
                continue;
            }
            int newcol = adjust_column(column, c, bflag);
            if (newcol > width && offset > 0) {    // adding c would overflow the line
                int brk = offset;
                if (sflag) while (brk > 0 && !is_blank(line_out[brk - 1])) brk--;
                if (sflag && brk > 0) {            // fold after the last blank, keep the tail
                    write(1, line_out, brk);
                    write(1, "\n", 1);
                    offset -= brk;
                    for (int i = 0; i < offset; i++) line_out[i] = line_out[brk + i];
                    column = 0;
                    for (int i = 0; i < offset; i++) column = adjust_column(column, line_out[i], bflag);
                } else {                           // no blank (or not -s): break right here
                    write(1, line_out, offset);
                    write(1, "\n", 1);
                    offset = 0; column = 0;
                }
                column = adjust_column(column, c, bflag);   // c opens the new line
            } else {
                column = newcol;
            }
            if (offset >= LINE_MAX) {              // pathological unbreakable line: bounded flush
                write(1, line_out, offset);
                offset = 0; column = adjust_column(0, c, bflag);
            }
            line_out[offset++] = c;
        }
    }
    if (offset) write(1, line_out, offset);        // trailing partial line, no added newline
}

static int parse_width(const char* v, int* out) {
    int w = 0, any = 0;
    for (const char* p = v; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        w = w * 10 + (*p - '0'); any = 1;
    }
    if (!any || w < 1) return -1;
    *out = w;
    return 0;
}

int main(int argc, char** argv) {
    int width = 80, bflag = 0, sflag = 0, ai = 1;

    for (; ai < argc && argv[ai][0] == '-' && argv[ai][1]; ai++) {
        if (argv[ai][1] == '-' && argv[ai][2] == '\0') { ai++; break; }   // "--"
        for (const char* p = argv[ai] + 1; *p; p++) {
            if (*p == 'b') bflag = 1;
            else if (*p == 's') sflag = 1;
            else if (*p == 'w') {
                const char* v = p[1] ? p + 1 : (ai + 1 < argc ? argv[++ai] : 0);
                if (!v || parse_width(v, &width) < 0) { printf("fold: invalid width\n"); return 1; }
                break;                             // width consumed the rest of this token
            } else { printf("fold: invalid option -- '%c'\n", *p); return 1; }
        }
    }

    if (ai >= argc) { fold_fd(0, width, bflag, sflag); return 0; }

    int rc = 0;
    for (; ai < argc; ai++) {
        if (argv[ai][0] == '-' && argv[ai][1] == '\0') { fold_fd(0, width, bflag, sflag); continue; }
        long f = open(argv[ai], O_RDONLY, 0);
        if (f < 0) { printf("fold: %s: cannot open\n", argv[ai]); rc = 1; continue; }
        fold_fd((int)f, width, bflag, sflag);
        close((int)f);
    }
    return rc;
}
