#ifndef FACTOR_H
#define FACTOR_H

#include "kernel.h"   // fixed-width types

// Prime-factorize n (with multiplicity, ascending) into out[0..cap). Returns the
// number of factors written (0 for n < 2). Trial division, overflow-safe for any
// uint64_t (the loop bound uses `d <= n/d`, never `d*d`). A 64-bit n has at most
// 63 prime factors, so cap >= 64 never truncates.
int factor_one(uint64_t n, uint64_t* out, int cap);

// Known-answer self-test (the `factor` CI KAT): 0 on success, else the failing case.
int factor_selftest(void);

#endif // FACTOR_H
