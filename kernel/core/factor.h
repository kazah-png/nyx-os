#ifndef FACTOR_H
#define FACTOR_H

#include "kernel.h"   // fixed-width types

// Prime-factorize n (with multiplicity, ascending) into out[0..cap). Returns the
// number of factors written (0 for n < 2). Small factors are peeled by trial
// division; the remaining cofactor is split with Miller-Rabin + Pollard's rho, so any
// 64-bit n factors fast. A 64-bit n has at most 63 prime factors, so cap >= 64 never
// truncates.
int factor_one(uint64_t n, uint64_t* out, int cap);

// Deterministic (exact) primality for any 64-bit n via Miller-Rabin with the twelve
// witnesses 2..37. 1 if prime, 0 otherwise. Backs the `factor` engine, the `isprime`
// command, and kernel.c's `mathx` KAT.
int is_prime_u64(uint64_t n);

// Known-answer self-test (the `factor` CI KAT): 0 on success, else the failing case.
int factor_selftest(void);

#endif // FACTOR_H
