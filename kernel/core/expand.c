#include "expand.h"

// Extracted verbatim from the `expand` builtin so the tab-stop math is defined ONCE and can be
// unit-tested off the kernel stack. A tab jumps to the next stop at (col/tabw + 1)*tabw — so a
// tab landing exactly on a stop still advances a full tabw — and a newline resets the column.
void expand_run(const char* buf, int len, int tabw, expand_emit_fn emit, void* ctx) {
    if (tabw < 1) tabw = 1;
    if (len < 0) len = 0;
    int col = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\t') {
            int next = (col / tabw + 1) * tabw;
            while (col < next) { emit(' ', ctx); col++; }
        } else if (c == '\n') {
            emit('\n', ctx); col = 0;
        } else {
            emit(c, ctx); col++;
        }
    }
}

// ---- known-answer self-test (`expand`) ----------------------------------------------------
typedef struct { char* out; int n; int cap; } exp_rec_t;
static void exp_rec_emit(char c, void* ctx) {
    exp_rec_t* r = (exp_rec_t*)ctx;
    if (r->n < r->cap - 1) r->out[r->n] = c;   // keep room for a terminator; count either way
    r->n++;
}
// Returns 1 iff expand_run(in, |in|, tabw) emits exactly `want`.
static int exp_check(const char* in, int tabw, const char* want) {
    int len = 0; while (in[len]) len++;
    char got[128]; exp_rec_t r; r.out = got; r.n = 0; r.cap = (int)sizeof got;
    expand_run(in, len, tabw, exp_rec_emit, &r);
    int wl = 0; while (want[wl]) wl++;
    if (r.n != wl) return 0;
    for (int i = 0; i < wl; i++) if (got[i] != want[i]) return 0;
    return 1;
}
int expand_selftest(void) {
    if (!exp_check("\tx",           8, "        x"))            return 1;  // tab at col 0 -> 8 spaces
    if (!exp_check("ab\tc",         8, "ab      c"))            return 2;  // col 2 -> next stop 8 (6 spaces)
    if (!exp_check("a\tb",          4, "a   b"))                return 3;  // width 4: col 1 -> 4 (3 spaces)
    if (!exp_check("a\t\n\tb",      8, "a       \n        b"))  return 4;  // newline resets the column
    if (!exp_check("hello",         8, "hello"))                return 5;  // no tabs -> passthrough
    if (!exp_check("12345678\tx",   8, "12345678        x"))    return 6;  // tab on a stop advances a full width
    if (!exp_check("a\tb",          1, "a b"))                  return 7;  // width 1 -> one space
    if (!exp_check("",              8, ""))                     return 8;  // empty
    return 0;
}
