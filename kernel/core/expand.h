#ifndef NYX_EXPAND_H
#define NYX_EXPAND_H
#include "types.h"

typedef void (*expand_emit_fn)(char c, void* ctx);

// Expand tabs in buf[0..len) to spaces: each '\t' advances the column to the next multiple of
// tabw (clamped to >=1), '\n' resets the column to 0, and every other byte passes through. Each
// output character is delivered via emit(c, ctx). Pure — no I/O; shared by the `expand` builtin
// and its self-test.
void expand_run(const char* buf, int len, int tabw, expand_emit_fn emit, void* ctx);

int expand_selftest(void);   // known-answer test of expand_run

#endif // NYX_EXPAND_H
