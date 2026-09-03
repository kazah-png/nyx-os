#ifndef JSON_H
#define JSON_H
#include "kernel.h"
// Strict RFC 8259 JSON validator. ITERATIVE (explicit bounded stack), so it uses O(1) C-stack
// regardless of nesting depth — safe to run on untrusted input on NyxOS's 4 KB kernel task
// stacks; nesting beyond 256 levels is rejected as a hardening measure. Accept/reject is verified
// byte-for-byte against Python's json across thousands of documents (scratchpad/json_proto.c).
// Returns 0 if `s` (NUL-terminated) is a valid JSON document, else -1 and, if errpos is non-NULL,
// sets *errpos to the byte offset of the first error.
int json_validate(const char* s, int* errpos);
int json_selftest(void);   // KAT: valid/invalid vectors + control-char + deep-nesting rejection

// json_query return codes (negative = failure).
enum { JQ_OK = 0, JQ_ENOTFOUND = -1, JQ_ETYPE = -2, JQ_EPATH = -3, JQ_EMALFORMED = -4 };

// jq-lite value extractor. Walks `path` into an (already valid) JSON document `s` and, on success,
// returns 0 with *out_start/*out_len set to the matched value's byte span inside `s` (no copy;
// scalar or whole subtree). `path` is a sequence of `.name` object-member steps and `[N]` array-
// index steps, e.g. `.users[0].name`; a leading `.` is optional and `.`/"" selects the whole
// document. Keys are matched by raw bytes (keys containing `.`/`[`/escapes are not addressable).
// Iterative like json_validate — O(1) C-stack regardless of nesting, safe on the 4 KB task stack.
// Errors: JQ_ENOTFOUND (missing key / index out of range), JQ_ETYPE (`.k` on a non-object or `[i]`
// on a non-array), JQ_EPATH (malformed selector), JQ_EMALFORMED (input wasn't valid JSON).
int json_query(const char* s, const char* path, int* out_start, int* out_len);
int json_query_selftest(void);   // KAT: fixed doc + path->span and path->error vectors

// Pretty-print a VALIDATED JSON string with 2-space indentation, emitting char-by-char via
// emit(c, ctx). Assumes `s` (length `len`) already passed json_validate. Used by `json fmt`.
void json_format(const char* s, int len, void (*emit)(char, void*), void* ctx);
int json_fmt_selftest(void);     // KAT: minified doc -> exact indented text (escapes, empty {}/[])
#endif
