#include "libc.h"

/* edit — a small full-screen text editor (nano-style). It draws onto the
 * terminal's cursor-addressed "screen mode" (v5.8.25): each keystroke it clears
 * the screen (ESC[2J), positions the cursor per row (ESC[r;cH) and repaints the
 * visible slice of the file plus a status bar, then parks the block cursor at the
 * editing point. Input is a raw-tty readkey() loop (arrows + editing keys +
 * Ctrl-O save / Ctrl-X exit). The desktop keeps compositing while we run
 * (v5.8.24), so the editor is live in the GUI window.
 *
 * Buffers are .bss statics: read()/getprocs() copy into them from the kernel and
 * cannot fault an unfaulted lazy-heap page. */

#define MAXLINES  200
#define MAXCOL    200
#define SCREEN_W  80
#define TEXTROWS  22          /* text rows; status bar is on row TEXTROWS */

/* Extended keycodes (must match kernel.h); readkey() returns these for nav keys. */
#define K_UP     0x80
#define K_DOWN   0x81
#define K_LEFT   0x82
#define K_RIGHT  0x83
#define K_HOME   0x84
#define K_END    0x85
#define K_PGUP   0x86
#define K_PGDN   0x87
#define K_DEL    0x89
#define CTRL_O   0x0F
#define CTRL_X   0x18

static char buf[MAXLINES][MAXCOL];
static int  nlines = 0;
static int  cy = 0, cx = 0;   /* cursor line/column in the file */
static int  top = 0;          /* first visible file line */
static int  dirty = 0;
static char fname[64];
static char msg[48];          /* transient status message ("Saved", ...) */

static char scr[16384];       /* one full frame, written in a single write() */
static int  sp;

static void s_putc(char c) { if (sp < (int)sizeof(scr) - 1) scr[sp++] = c; }
static void s_str(const char* s) { while (*s) s_putc(*s++); }
static void s_int(int v) {
    char t[12]; int n = 0;
    if (v < 0) { s_putc('-'); v = -v; }
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) s_putc(t[--n]);
}
static void s_goto(int row, int col) { s_str("\x1b["); s_int(row); s_putc(';'); s_int(col); s_putc('H'); }

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }

static void load(void) {
    long fd = open(fname, O_RDONLY, 0);
    if (fd < 0) { nlines = 0; return; }          /* new file */
    static char fb[8192];
    long total = 0, r;
    while (total < (long)sizeof(fb) - 1 &&
           (r = read((int)fd, fb + total, sizeof(fb) - 1 - total)) > 0)
        total += r;
    close((int)fd);
    fb[total] = '\0';
    nlines = 0; int col = 0;
    for (long i = 0; i < total && nlines < MAXLINES; i++) {
        char ch = fb[i];
        if (ch == '\n') { buf[nlines][col] = '\0'; nlines++; col = 0; }
        else if (ch != '\r' && col < MAXCOL - 1) buf[nlines][col++] = ch;
    }
    if (col > 0 && nlines < MAXLINES) { buf[nlines][col] = '\0'; nlines++; }
}

static void save(void) {
    long fd = open(fname, O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { strcpy(msg, "SAVE FAILED"); return; }
    long bytes = 0;
    for (int i = 0; i < nlines; i++) {
        int L = slen(buf[i]);
        if (L > 0) { write((int)fd, buf[i], L); bytes += L; }
        write((int)fd, "\n", 1); bytes++;
    }
    close((int)fd);
    dirty = 0;
    sprintf(msg, "Saved %d lines, %d bytes", nlines, (int)bytes);
}

static void render(void) {
    sp = 0;
    s_str("\x1b[2J");                              /* clear -> screen mode */
    for (int r = 0; r < TEXTROWS; r++) {
        s_goto(r + 1, 1);
        int fl = top + r;
        if (fl < nlines) {
            char* L = buf[fl];
            for (int c = 0; c < SCREEN_W && L[c]; c++) s_putc(L[c]);
        } else {
            s_putc('~');
        }
    }
    /* status bar */
    s_goto(TEXTROWS + 1, 1);
    s_str(" ");
    s_str(fname);
    s_str(dirty ? " *" : "  ");
    s_str("  Ln "); s_int(cy + 1); s_putc('/'); s_int(nlines);
    s_str("  Col "); s_int(cx + 1);
    s_str("   ^O Save  ^X Exit");
    if (msg[0]) { s_str("   "); s_str(msg); }
    /* park the cursor at the editing point (screen-relative, 1-based) */
    s_goto(cy - top + 1, cx + 1);
    write(1, scr, sp);
}

static void ins_char(char ch) {
    char* L = buf[cy];
    int len = slen(L);
    if (len >= MAXCOL - 1) return;
    for (int i = len + 1; i > cx; i--) L[i] = L[i - 1];  /* shift right incl NUL */
    L[cx] = ch; cx++; dirty = 1;
}

static void split_line(void) {
    if (nlines >= MAXLINES) return;
    for (int i = nlines; i > cy + 1; i--)                /* make room at cy+1 */
        for (int j = 0; j < MAXCOL; j++) buf[i][j] = buf[i - 1][j];
    char* L = buf[cy];
    int len = slen(L), t = 0;
    for (int i = cx; i <= len; i++) buf[cy + 1][t++] = L[i];   /* tail (incl NUL) */
    L[cx] = '\0';
    nlines++; cy++; cx = 0; dirty = 1;
}

static void backspace(void) {
    if (cx > 0) {
        char* L = buf[cy]; int len = slen(L);
        for (int i = cx - 1; i <= len; i++) L[i] = L[i + 1];  /* shift left incl NUL */
        cx--; dirty = 1;
    } else if (cy > 0) {
        int plen = slen(buf[cy - 1]), clen = slen(buf[cy]);
        if (plen + clen < MAXCOL - 1)
            for (int i = 0; i <= clen; i++) buf[cy - 1][plen + i] = buf[cy][i];
        for (int i = cy; i < nlines - 1; i++)                 /* remove line cy */
            for (int j = 0; j < MAXCOL; j++) buf[i][j] = buf[i + 1][j];
        nlines--; cy--; cx = plen; dirty = 1;
    }
}

int main(int argc, char** argv) {
    if (argc >= 2) { strncpy(fname, argv[1], sizeof(fname) - 1); load(); }
    else strcpy(fname, "noname.txt");
    if (nlines == 0) { nlines = 1; buf[0][0] = '\0'; }

    ttymode(TTY_RAW);
    for (;;) {
        if (cy < 0) cy = 0;
        if (cy >= nlines) cy = nlines - 1;
        int ll = slen(buf[cy]);
        if (cx > ll) cx = ll;
        if (cx < 0) cx = 0;
        if (cy < top) top = cy;                     /* scroll to keep cursor visible */
        if (cy >= top + TEXTROWS) top = cy - TEXTROWS + 1;
        if (top < 0) top = 0;

        render();

        long k = readkey(0);                        /* block until a key */
        if (k <= 0) continue;
        msg[0] = '\0';                              /* clear transient message on any key */

        if (k == CTRL_X) break;
        else if (k == CTRL_O) save();
        else if (k == K_UP)    cy--;
        else if (k == K_DOWN)  cy++;
        else if (k == K_LEFT)  { if (cx > 0) cx--; else if (cy > 0) { cy--; cx = slen(buf[cy]); } }
        else if (k == K_RIGHT) { if (cx < ll) cx++; else if (cy < nlines - 1) { cy++; cx = 0; } }
        else if (k == K_HOME)  cx = 0;
        else if (k == K_END)   cx = ll;
        else if (k == K_PGUP)  cy -= TEXTROWS;
        else if (k == K_PGDN)  cy += TEXTROWS;
        else if (k == '\n' || k == '\r') split_line();
        else if (k == 0x08 || k == 0x7F) backspace();
        else if (k == K_DEL) {                      /* forward-delete = advance + backspace */
            if (cx < ll) { cx++; backspace(); }
            else if (cy < nlines - 1) { cy++; cx = 0; backspace(); }
        }
        else if (k >= 0x20 && k <= 0x7E) ins_char((char)k);
    }

    ttymode(TTY_CANON);
    printf("edit: done (%s)\n", fname);
    return 0;
}
