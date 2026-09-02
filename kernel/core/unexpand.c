#include "unexpand.h"

// Emit a run of blank columns [from, to) as the FEWEST tabs+spaces that reproduce it: step tab
// stop to tab stop, using a tab whenever it collapses >= 2 columns (a single column to the next
// stop stays a space, since one blank must never become a tab), then any leftover columns short
// of a stop as spaces. Extracted verbatim from the `unexpand` builtin's unexpand_flush.
static void unexpand_flush_emit(int from, int to, int tabw, unexpand_emit_fn emit, void* ctx) {
    int c = from;
    while ((c / tabw + 1) * tabw <= to) {
        int nt = (c / tabw + 1) * tabw;
        if (nt - c >= 2) { emit('\t', ctx); c = nt; }
        else             { emit(' ', ctx);  c++;    }
    }
    while (c < to) { emit(' ', ctx); c++; }
}

// Extracted verbatim from the `unexpand` builtin so the column/collapse logic is defined ONCE and
// unit-testable off the kernel stack. `convert` gates whether blanks are buffered into a run
// [runstart, col) that is flushed at the next non-blank / newline / EOF.
void unexpand_run(const char* buf, int len, int tabw, int all, unexpand_emit_fn emit, void* ctx) {
    if (tabw < 1) tabw = 1;
    if (len < 0) len = 0;
    int col = 0, run = 0, runstart = 0, convert = 1;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (convert && (c == ' ' || c == '\t')) {
            if (run == 0) runstart = col;
            col = (c == '\t') ? (col / tabw + 1) * tabw : col + 1;
            run = col - runstart;
        } else {
            if (run > 0) { unexpand_flush_emit(runstart, col, tabw, emit, ctx); run = 0; }
            if (c == '\n')      { emit('\n', ctx); col = 0; convert = 1; }
            else if (c == '\t') { emit('\t', ctx); col = (col / tabw + 1) * tabw; }
            else if (c == ' ')  { emit(' ', ctx);  col++; }
            else                { emit(c, ctx); col++; if (!all) convert = 0; }
        }
    }
    if (run > 0) unexpand_flush_emit(runstart, col, tabw, emit, ctx);
}

// ---- known-answer self-test (`unexpand`) --------------------------------------------------
typedef struct { char* out; int n; int cap; } unx_rec_t;
static void unx_rec_emit(char c, void* ctx) {
    unx_rec_t* r = (unx_rec_t*)ctx;
    if (r->n < r->cap - 1) r->out[r->n] = c;
    r->n++;
}
static int unx_check(const char* in, int tabw, int all, const char* want) {
    int len = 0; while (in[len]) len++;
    char got[128]; unx_rec_t r; r.out = got; r.n = 0; r.cap = (int)sizeof got;
    unexpand_run(in, len, tabw, all, unx_rec_emit, &r);
    int wl = 0; while (want[wl]) wl++;
    if (r.n != wl) return 0;
    for (int i = 0; i < wl; i++) if (got[i] != want[i]) return 0;
    return 1;
}
int unexpand_selftest(void) {
    if (!unx_check("        x",  8, 0, "\tx"))          return 1;  // 8 leading spaces -> one tab
    if (!unx_check("x        y", 8, 0, "x        y"))   return 2;  // default: interior blanks left alone
    if (!unx_check("\tx",        8, 0, "\tx"))          return 3;  // a literal input tab passes through
    if (!unx_check("",           8, 0, ""))             return 4;  // empty
    if (!unx_check("       x",   8, 0, "       x"))     return 5;  // 7 spaces: a tab would overshoot -> stays spaces
    if (!unx_check("x\n    y",   4, 0, "x\n\ty"))       return 6;  // newline re-enables leading conversion (width 4)
    if (!unx_check("x        y", 8, 1, "x\t y"))        return 7;  // -a converts the interior run (tab to col 8 + 1 space)
    return 0;
}
