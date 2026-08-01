#include "libc.h"

/* tac — write a file (or stdin) with its lines in reverse order: the complement
 * of cat (tac is cat spelled backwards). Reads the whole input, splits it into
 * lines (a line is the run of bytes up to a '\n', the newline excluded), then
 * emits the lines last-to-first, each newline-terminated. Empty lines are kept.
 * Bounded to a fixed buffer; input beyond the cap is dropped with a note. */

#define TAC_CAP   32768
#define TAC_LINES  4096

static char buf[TAC_CAP];
static int  line_off[TAC_LINES];
static int  line_len[TAC_LINES];

int main(int argc, char** argv) {
    int fd = 0;   /* stdin by default */
    if (argc > 1) {
        long f = open(argv[1], O_RDONLY, 0);
        if (f < 0) { printf("tac: %s: not found\n", argv[1]); return 1; }
        fd = (int)f;
    }

    int total = 0, n;
    while (total < TAC_CAP && (n = (int)read(fd, buf + total, TAC_CAP - total)) > 0)
        total += n;
    if (fd != 0) close(fd);
    if (total >= TAC_CAP) printf("tac: input truncated at %d bytes\n", TAC_CAP);

    /* Split into lines (content only, without the terminating '\n'). */
    int nlines = 0, i = 0;
    while (i < total) {
        int start = i;
        while (i < total && buf[i] != '\n') i++;
        if (nlines < TAC_LINES) { line_off[nlines] = start; line_len[nlines] = i - start; nlines++; }
        if (i < total) i++;                 /* skip the '\n' */
    }

    /* Emit last line first, each newline-terminated. */
    for (int k = nlines - 1; k >= 0; k--) {
        write(1, buf + line_off[k], line_len[k]);
        write(1, "\n", 1);
    }
    return 0;
}
