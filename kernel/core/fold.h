#ifndef NYX_FOLD_H
#define NYX_FOLD_H
#include "types.h"

typedef void (*fold_emit_fn)(char c, void* ctx);

// Hard-wrap buf[0..len) so no output line exceeds `width` columns (clamped to >=1): a real '\n'
// passes through and resets the column; otherwise a '\n' is inserted just before the character
// that would overflow. Non-newline bytes (including tabs) each count as one column, matching the
// `fold` builtin. Each output byte is delivered via emit(c, ctx). Pure — no I/O; shared by the
// `fold` builtin and its self-test.
void fold_run(const char* buf, int len, int width, fold_emit_fn emit, void* ctx);

int fold_selftest(void);   // known-answer test of fold_run

#endif // NYX_FOLD_H
