#ifndef NYX_SEQ_H
#define NYX_SEQ_H
// seq(1) core - generate an inclusive integer sequence FIRST, FIRST+STEP, ... bounded
// by LAST, invoking emit() for each value. Pure, allocation-free, and overflow-safe
// across the whole signed 64-bit range: the running value never leaves [FIRST,LAST],
// and the term count is computed in unsigned arithmetic so no intermediate wraps.
// Matches GNU `seq` for integer arguments - a positive step walks up while the value
// stays <= LAST, a negative step walks down while it stays >= LAST, and a range that
// starts on the wrong side of LAST emits nothing.
#include "../core/kernel.h"

typedef void (*seq_emit_fn)(long long value, void* ctx);

// Returns 0 on success (possibly zero emits, for an empty range); -1 if step == 0
// (the caller reports the error, matching GNU's "invalid zero increment").
int seq_run(long long first, long long step, long long last, seq_emit_fn emit, void* ctx);

// Known-answer self-test: pins GNU-seq (integer) parity + the 64-bit overflow-safety edges. 0 on pass.
int seq_selftest(void);
#endif
