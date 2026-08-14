#ifndef NYX_FNV_H
#define NYX_FNV_H
// Fowler-Noll-Vo 1a - a small, fast, NON-cryptographic hash (byte-at-a-time
// XOR-then-multiply). The workhorse behind many hash tables / dedup indexes:
// cheap, good avalanche for a non-crypto hash, and deterministic across runs and
// platforms. NOT collision-resistant - never use it where an adversary controls the
// keys (reach for SipHash there). The "1a" ordering (XOR the byte, then multiply)
// mixes better than the original FNV-1. Vectors match the FNV reference test suite.
#include "../core/kernel.h"

#define FNV1A_32_INIT  0x811c9dc5u            // 32-bit offset basis (2166136261)
#define FNV1A_64_INIT  0xcbf29ce484222325ull  // 64-bit offset basis

// One-shot hash of buf[len].
uint32_t fnv1a_32(const void* buf, uint32_t len);
uint64_t fnv1a_64(const void* buf, uint32_t len);

// Incremental: seed with the matching FNV1A_*_INIT, fold each chunk, chain the
// result. Folding the whole input in one call equals folding it in pieces.
uint32_t fnv1a_32_update(uint32_t h, const void* buf, uint32_t len);
uint64_t fnv1a_64_update(uint64_t h, const void* buf, uint32_t len);

int fnv_selftest(void);
#endif
