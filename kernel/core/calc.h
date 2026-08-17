#ifndef CALC_H
#define CALC_H
#include "kernel.h"
// Integer expression evaluator — 64-bit signed, C operator set and precedence:
//   |  ^  &  << >>  + -  * / %  unary(- + ~)  ( )   with decimal / 0x hex / 0b binary literals.
// Truncating division and C-style modulo, arithmetic right shift, two's-complement wraparound.
// Pure and allocation-free; the logic is host-verified against an independent C-int64 model
// (scratchpad/calc_proto.c) across thousands of random expressions. calc_eval returns 0 and
// sets *out on success, or the negated error code:
//   1 syntax/bad token · 2 unbalanced parens · 3 divide by zero · 4 trailing junk ·
//   5 shift out of 0..63 · 6 empty expression.
int calc_eval(const char* expr, int64_t* out);
int calc_selftest(void);   // KAT: precedence/associativity/literals/C-semantics + error cases
#endif
