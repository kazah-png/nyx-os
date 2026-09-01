#include "seq.h"

// The running value stays within [FIRST, LAST], so `v += step` never overflows. The
// only quantity that could is the span; it is computed in unsigned arithmetic, where
// (uint64)LAST - (uint64)FIRST is the exact distance even across the sign boundary,
// and 0 - (uint64)step yields |step| exactly even for step == INT64_MIN.
int seq_run(long long first, long long step, long long last, seq_emit_fn emit, void* ctx) {
    if (step == 0) return -1;                                   // invalid zero increment
    unsigned long long n;                                       // count of terms AFTER the first
    if (step > 0) {
        if (first > last) return 0;                             // empty ascending range
        unsigned long long span = (unsigned long long)last - (unsigned long long)first;
        n = span / (unsigned long long)step;
    } else {
        if (first < last) return 0;                             // empty descending range
        unsigned long long span = (unsigned long long)first - (unsigned long long)last;
        n = span / (0ULL - (unsigned long long)step);           // divide by |step|
    }
    long long v = first;
    for (unsigned long long i = 0; ; i++) {
        emit(v, ctx);
        if (i == n) break;
        v += step;
    }
    return 0;
}

// ---- known-answer self-test (`seq`) ------------------------------------------------------
typedef struct { long long* vals; int n; int cap; } seq_rec_t;
static void seq_rec_emit(long long v, void* ctx) {
    seq_rec_t* r = (seq_rec_t*)ctx;
    if (r->n < r->cap) r->vals[r->n] = v;
    r->n++;
}
// Run seq_run(first,step,last); return 1 iff it returns `wrc` and emits exactly want[0..wn).
static int seq_check(long long first, long long step, long long last,
                     const long long* want, int wn, int wrc) {
    long long got[40]; seq_rec_t r; r.vals = got; r.n = 0; r.cap = 40;
    int rc = seq_run(first, step, last, seq_rec_emit, &r);
    if (rc != wrc || r.n != wn) return 0;
    for (int i = 0; i < wn; i++) if (got[i] != want[i]) return 0;
    return 1;
}
// Pins GNU-seq (integer) parity + the overflow-safety edges: ascending step, an inexact end,
// descending, an exact end, a single value, both empty directions, step 0 (error, no emit), a
// negative start crossing zero, and a near-INT64_MAX span that must not wrap.
int seq_selftest(void) {
    { long long w[] = {1,2,3,4,5};  if (!seq_check(1,1,5,   w,5,0))  return 1; }
    { long long w[] = {1,3,5,7,9};  if (!seq_check(1,2,10,  w,5,0))  return 2; }
    { long long w[] = {5,4,3,2,1};  if (!seq_check(5,-1,1,  w,5,0))  return 3; }
    { long long w[] = {1,4,7,10};   if (!seq_check(1,3,10,  w,4,0))  return 4; }
    { long long w[] = {7};          if (!seq_check(7,1,7,   w,1,0))  return 5; }
    {                               if (!seq_check(5,1,3,   0,0,0))  return 6; }   // empty ascending
    {                               if (!seq_check(1,-1,5,  0,0,0))  return 7; }   // empty descending
    {                               if (!seq_check(1,0,5,   0,0,-1)) return 8; }   // zero step -> error
    { long long w[] = {-2,0,2};     if (!seq_check(-2,2,3,  w,3,0))  return 9; }   // negative start over zero
    { long long w[] = {9223372036854775805LL, 9223372036854775807LL};
      if (!seq_check(9223372036854775805LL, 2, 9223372036854775807LL, w, 2, 0)) return 10; }
    return 0;
}
