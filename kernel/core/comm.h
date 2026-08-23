#ifndef COMM_H
#define COMM_H

#include "kernel.h"   // fixed-width types

// Line-by-line merge of two texts that are each already SORTED (byte / C-locale
// order) — the engine behind the `comm` coreutil. For every output line it invokes
// emit(tabs, line, len, ctx): `tabs` leading tab stops (0/1/2) then the line content
// (no trailing newline; the caller adds one). The three columns are: 1 = lines only in
// A, 2 = lines only in B, 3 = lines common to both; show1/show2/show3 select which are
// emitted (a suppressed column also removes its tab stop from later columns, matching
// GNU comm's -1/-2/-3). Allocation-free; every read is bounded by alen/blen.
typedef void (*comm_emit_fn)(int tabs, const char* line, int len, void* ctx);

void comm_run(const char* a, int alen, const char* b, int blen,
              int show1, int show2, int show3, comm_emit_fn emit, void* ctx);

// KAT: three-column merge + tab-stop suppression + tail drain + no-trailing-newline. 0 = pass.
int comm_selftest(void);

#endif // COMM_H
