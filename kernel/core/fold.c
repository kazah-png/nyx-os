#include "fold.h"

// Extracted verbatim from the `fold` builtin so the wrap rule is defined ONCE and unit-testable
// off the kernel stack. A line exactly `width` long is NOT wrapped — the break is inserted only
// when the NEXT non-newline character would push the column past width (so the wrap point sits
// before the overflowing char), and a real newline always passes through and resets the column.
void fold_run(const char* buf, int len, int width, fold_emit_fn emit, void* ctx) {
    if (width < 1) width = 1;
    if (len < 0) len = 0;
    int col = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') { emit('\n', ctx); col = 0; continue; }   // keep real line breaks
        if (col >= width) { emit('\n', ctx); col = 0; }          // hard-wrap before this char
        emit(c, ctx);
        col++;
    }
}

// ---- known-answer self-test (`fold`) ------------------------------------------------------
typedef struct { char* out; int n; int cap; } fold_rec_t;
static void fold_rec_emit(char c, void* ctx) {
    fold_rec_t* r = (fold_rec_t*)ctx;
    if (r->n < r->cap - 1) r->out[r->n] = c;
    r->n++;
}
static int fold_check(const char* in, int width, const char* want) {
    int len = 0; while (in[len]) len++;
    char got[128]; fold_rec_t r; r.out = got; r.n = 0; r.cap = (int)sizeof got;
    fold_run(in, len, width, fold_rec_emit, &r);
    int wl = 0; while (want[wl]) wl++;
    if (r.n != wl) return 0;
    for (int i = 0; i < wl; i++) if (got[i] != want[i]) return 0;
    return 1;
}
int fold_selftest(void) {
    if (!fold_check("abcd\n",     4, "abcd\n"))       return 1;  // exactly width -> no spurious wrap
    if (!fold_check("abcde",      4, "abcd\ne"))      return 2;  // one over -> wrap before the 5th char
    if (!fold_check("ab",         1, "a\nb"))         return 3;  // width 1
    if (!fold_check("ab\ncd",     4, "ab\ncd"))       return 4;  // real newlines preserved (reset the column)
    if (!fold_check("abcdefgh",   4, "abcd\nefgh"))   return 5;  // wrap continues cleanly
    if (!fold_check("abcd\nefghi",4, "abcd\nefgh\ni"))return 6;  // newline resets, then the next line wraps
    if (!fold_check("",           4, ""))             return 7;  // empty
    return 0;
}
