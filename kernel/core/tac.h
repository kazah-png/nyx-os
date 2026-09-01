#ifndef NYX_TAC_H
#define NYX_TAC_H
// tac(1) core - print the records of a text in REVERSE order. A record is the bytes up
// to and INCLUDING the next occurrence of the separator (GNU's "separator = terminator"
// model), so a trailing separator travels with its record when reversed, and the final
// record may lack a separator (e.g. a file with no trailing newline: `a\nb\nc` reverses
// to `cb\na\n`, matching GNU tac). Pure and allocation-free: the caller supplies a
// `starts` scratch array for the record offsets, and each reversed record is handed to
// `emit` as a byte span.
#include "kernel.h"

typedef void (*tac_emit_fn)(const char* data, uint32_t len, void* ctx);

// Emit the records of text[0..len) in reverse. sep[0..seplen) is the record separator
// (>= 1 byte; the default caller passes "\n"). starts[maxrecs] is caller scratch and
// must hold at least (separator count + 1) offsets — cmd_tac sizes it past the input
// bound so it never overflows; if it ever did, the earliest records simply stop being
// split (no bytes are lost).
void tac_run(const char* text, uint32_t len, const char* sep, uint32_t seplen,
             uint32_t* starts, uint32_t maxrecs, tac_emit_fn emit, void* ctx);

// Known-answer self-test: pins GNU-tac record-reverse parity (separator-as-terminator). Returns 0 on pass.
int tac_selftest(void);
#endif
