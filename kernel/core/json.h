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
#endif
