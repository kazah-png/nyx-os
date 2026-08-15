#ifndef NYX_MURMUR3_H
#define NYX_MURMUR3_H
// MurmurHash3 (x86_32 variant, Austin Appleby, public domain) — a fast NON-cryptographic hash
// with excellent avalanche/distribution, the workhorse behind hash tables, bloom filters and
// content sharding. It is NOT collision-resistant against an adversary (use SipHash for that);
// it is chosen where speed and good spread matter. A distinct algorithm from the FNV-1a and
// SipHash hashes and the CRC/Fletcher checksums already here. Correctness is pinned by
// murmur3_selftest() (the CI `murmur` KAT).
#include "../core/kernel.h"

// 32-bit MurmurHash3 of data[0..len) with the given seed.
uint32_t murmur3_32(const uint8_t* data, uint32_t len, uint32_t seed);

int murmur3_selftest(void);   // 0 = pass; else the failing vector's 1-based index

#endif
