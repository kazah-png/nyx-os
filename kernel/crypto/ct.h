#ifndef CT_H
#define CT_H
// Constant-time memory comparison. Secret-dependent comparisons — AEAD tags, a MAC,
// the TLS Finished verify_data, a password hash — must not branch or return early on
// the data: a compare that stops at the first mismatching byte leaks, through its
// running time, how many leading bytes an attacker's guess got right, which turns a
// blind forgery into a byte-at-a-time search (the classic MAC-verification timing
// attack). ct_memcmp always reads all n bytes and folds every difference into one
// accumulator, so its running time depends on n alone, never on the contents. It is
// the single audited comparator the AEAD (aes_gcm, chacha20poly1305) and TLS verify
// paths share, so the property is established in one place instead of re-derived (and
// re-broken) at each call site.
#include "../core/kernel.h"

// Returns 0 iff the first n bytes of a and b are equal, non-zero otherwise (the same
// sense as memcmp's equality result), in time independent of the byte values.
int ct_memcmp(const void* a, const void* b, uint32_t n);

// Known-answer test (`cttest`): equal buffers, and a lone difference placed at the
// first / a middle / the LAST byte — the last being the case an early-return compare
// takes longest on, so it proves every byte is examined. Returns 0 if all pass.
int ct_selftest(void);

#endif // CT_H
