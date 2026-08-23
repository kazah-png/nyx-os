#ifndef NYX_JOIN_H
#define NYX_JOIN_H
// join — relational join of two files on a common field, byte-for-byte with GNU join. Both
// inputs must already be sorted on the join field (a merge join, like GNU; not verified here).
// For each pair of input lines whose join fields are equal it prints the join field, then the
// remaining fields of the first file, then the remaining fields of the second — and emits the
// full cartesian product when a key repeats. The pure logic lives here so it can be unit-tested
// off-target; cmd_join just parses argv and reads the two files.
#include "kernel.h"

typedef struct {
    char     delim;   // field separator; 0 = default (runs of blanks, leading blanks skipped)
    uint32_t jf1;     // 1-based join field in file 1
    uint32_t jf2;     // 1-based join field in file 2
    int      a1;      // also print unpaired lines from file 1 (-a 1)
    int      a2;      // also print unpaired lines from file 2 (-a 2)
} join_opts_t;

// Called once per output record with the final bytes (no newline); the consumer adds a newline.
typedef void (*join_emit_fn)(void* ctx, const char* out, uint32_t len);

// Merge-join t1[0..l1) and t2[0..l2) per the options, emitting joined and (optionally) unpaired
// records in key order. Each emitted record excludes its trailing newline.
void join_run(const char* t1, uint32_t l1, const char* t2, uint32_t l2,
              const join_opts_t* o, join_emit_fn emit, void* ctx);

// KAT: inner join + -a1 unpaired + cartesian product + custom delimiter + differing key fields. 0=pass.
int join_selftest(void);

#endif
