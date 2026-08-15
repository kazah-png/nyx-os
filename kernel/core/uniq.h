#ifndef NYX_UNIQ_H
#define NYX_UNIQ_H
// uniq — collapse ADJACENT equal lines, byte-for-byte with GNU uniq. Like GNU, it never sorts:
// only runs of neighbouring lines with equal comparison keys are folded, so it is the classic
// `sort | uniq` companion. The comparison key is the line after skipping -f leading fields and
// -s leading characters, limited to -w characters, optionally case-insensitively (-i); the
// PRINTED line is always the first full line of the run. The pure logic lives here so it can be
// unit-tested off-target; cmd_uniq just parses argv and reads the file.
#include "kernel.h"

typedef struct {
    int count;            // -c: prefix each line with its occurrence count (7-wide, then a space)
    int only_dup;         // -d: only print lines that repeat (run length >= 2)
    int only_uniq;        // -u: only print lines that occur exactly once
    int ignore_case;      // -i: compare case-insensitively
    uint32_t skip_fields; // -f N: skip N leading blank-separated fields before comparing
    uint32_t skip_chars;  // -s N: then skip N leading characters before comparing
    uint32_t check_chars; // -w N: compare at most N characters of the key
    int check_chars_set;  // whether -w was given (else the key is unbounded)
} uniq_opts_t;

// Called once per emitted record with the final output bytes (the -c count prefix already
// applied, no newline); the consumer writes them followed by a single newline.
typedef void (*uniq_emit_fn)(void* ctx, const char* out, uint32_t len);

// Filter adjacent equal lines of text[0..len) per the options, emitting the first line of each
// surviving run in order (GNU uniq semantics).
void uniq_run(const uniq_opts_t* o, const char* text, uint32_t len, uniq_emit_fn emit, void* ctx);

#endif
