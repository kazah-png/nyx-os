#ifndef NYX_TSORT_H
#define NYX_TSORT_H
// tsort(1) core — topological sort. Reads whitespace-separated tokens in PAIRS; each pair
// "u v" is a dependency edge u->v (u must be emitted before v). Produces a linear ordering
// consistent with every edge (Kahn's algorithm, ties broken by first-appearance order to
// match GNU tsort on inputs whose order is forced). HARDENED for untrusted input: the node,
// name, and edge tables are all bounded and overflow is clamped (never written past), there
// is NO recursion (an adversarial deep chain can't blow the stack), and a cyclic graph is
// detected and reported instead of looping forever.
#include "kernel.h"

typedef void (*tsort_emit_fn)(const char* name, uint32_t len, void* ctx);

// Emit the nodes of text[0..len) in dependency order, one per emit() call.
// Returns: >= 0  -> success; the value is the number of nodes emitted (all of them);
//          -1     -> the input held an odd number of tokens (malformed, like GNU);
//          -2     -> a cycle was found; only the acyclic prefix was emitted.
int tsort_run(const char* text, uint32_t len, tsort_emit_fn emit, void* ctx);

#endif
