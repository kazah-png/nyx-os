#ifndef NYX_UNEXPAND_H
#define NYX_UNEXPAND_H
#include "types.h"

typedef void (*unexpand_emit_fn)(char c, void* ctx);

// unexpand — the inverse of expand: collapse runs of blanks in buf[0..len) back into tabs at
// tabw-column stops (clamped to >=1), emitting the FEWEST tabs+spaces that reproduce each run
// (a run that would need <2 columns to the next stop stays spaces). all=0 converts only each
// line's LEADING blanks (GNU default); all=1 converts blank runs anywhere. A '\n' resets the
// column and re-enables leading conversion; a literal input tab passes through. Each output byte
// is delivered via emit(c, ctx). Pure — no I/O; shared by the `unexpand` builtin and its self-test.
void unexpand_run(const char* buf, int len, int tabw, int all, unexpand_emit_fn emit, void* ctx);

int unexpand_selftest(void);   // known-answer test of unexpand_run

#endif // NYX_UNEXPAND_H
