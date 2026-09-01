#ifndef NYX_PASTE_H
#define NYX_PASTE_H
// paste(1) core - merge lines of files. Pure and allocation-free: it walks the
// in-memory file texts with a cursor per file and emits one output byte at a time
// through a callback. Two modes, matching GNU paste for '\n'-delimited text:
//   parallel (default): output row i joins the i-th line of every file with cycling
//     delimiters, continuing until EVERY file is exhausted (a spent file contributes
//     an empty field);
//   serial (serial != 0): for each file in turn, join ALL its lines onto one output
//     line with the cycling delimiter.
// Each output line ends with '\n'. A file's final line need not end in '\n'. The
// delimiter list must have >= 1 char (the default is a single '\t').
#include "kernel.h"

typedef void (*paste_emit_fn)(char c, void* ctx);

typedef struct { const char* text; uint32_t len; } paste_file_t;

void paste_run(const paste_file_t* files, int nfiles,
               const char* delims, int ndelims, int serial,
               paste_emit_fn emit, void* ctx);

// Known-answer self-test: pins GNU-paste parity (parallel/serial, empty fields, delim cycling). 0 on pass.
int paste_selftest(void);
#endif
