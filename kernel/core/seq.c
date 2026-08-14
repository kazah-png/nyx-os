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
