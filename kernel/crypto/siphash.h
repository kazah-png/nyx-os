#ifndef SIPHASH_H
#define SIPHASH_H

#include "../core/kernel.h"

/* SipHash-2-4 (Aumasson & Bernstein, 2012): a fast keyed pseudo-random function
 * over a 128-bit key. It is a MAC for short messages, but its headline use is
 * making hash tables resistant to collision-flooding denial of service — the
 * role it plays in Python, Rust, Perl, and the Linux kernel. 2 compression
 * rounds per 8-byte block, 4 finalisation rounds; pure 64-bit ARX. */
uint64_t siphash24(const uint8_t key[16], const uint8_t* data, uint32_t len);

/* Known-answer test against the reference SipHash-2-4 vectors. 0 = pass. */
int siphash_selftest(void);

#endif
