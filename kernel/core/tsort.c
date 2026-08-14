#include "tsort.h"

#define TS_MAX_NODES  256
#define TS_MAX_NAME    48          // token bytes kept (longer tokens are truncated)
#define TS_MAX_EDGES  2048

static int ts_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Byte-wise string compare (independent of any libc, identical on host and kernel), used to
// order the initial roots exactly the way GNU tsort's sorted node tree does.
static int ts_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

// Intern a token: return its existing node index, or add it (first-appearance order).
// Returns -1 only if the node table is full (the caller then reuses the last slot, which
// keeps the run bounded — real inputs stay well under TS_MAX_NODES).
static int ts_intern(char names[][TS_MAX_NAME], int* nnodes,
                     const char* tok, uint32_t tlen) {
    uint32_t keep = tlen < TS_MAX_NAME ? tlen : TS_MAX_NAME - 1;
    for (int i = 0; i < *nnodes; i++) {
        const char* nm = names[i];
        uint32_t j = 0;
        while (j < keep && nm[j] == tok[j]) j++;
        if (j == keep && nm[keep] == '\0') return i;      // exact match
    }
    if (*nnodes >= TS_MAX_NODES) return -1;
    int idx = (*nnodes)++;
    uint32_t k = 0;
    for (; k < keep; k++) names[idx][k] = tok[k];
    names[idx][k] = '\0';
    return idx;
}

int tsort_run(const char* text, uint32_t len, tsort_emit_fn emit, void* ctx) {
    static char names[TS_MAX_NODES][TS_MAX_NAME];
    static int  indeg[TS_MAX_NODES];
    static int  adj_head[TS_MAX_NODES];      // head edge index for each node, -1 = none
    static int  edge_to[TS_MAX_EDGES];
    static int  edge_next[TS_MAX_EDGES];
    static int  q[TS_MAX_NODES];             // Kahn ready-queue (FIFO)
    int nnodes = 0, nedges = 0;

    for (int i = 0; i < TS_MAX_NODES; i++) { indeg[i] = 0; adj_head[i] = -1; }

    // Tokenize and fold pairs into edges. The first token of a pair is `u`, the second is
    // `v`; a self-pair (u == v) adds no edge but still registers the node, matching GNU
    // (which orders a lone self-referential node without calling it a cycle).
    int have_u = 0, u = -1;
    uint32_t i = 0;
    while (i < len) {
        while (i < len && ts_is_space(text[i])) i++;
        if (i >= len) break;
        uint32_t start = i;
        while (i < len && !ts_is_space(text[i])) i++;
        int node = ts_intern(names, &nnodes, text + start, i - start);
        if (node < 0) node = nnodes - 1;                 // table full: clamp to last slot
        if (!have_u) { u = node; have_u = 1; }
        else {
            if (u != node && nedges < TS_MAX_EDGES) {
                edge_to[nedges] = node;
                edge_next[nedges] = adj_head[u];
                adj_head[u] = nedges;
                nedges++;
                indeg[node]++;
            }
            have_u = 0;
        }
    }
    if (have_u) return -1;                                // odd number of tokens

    // Kahn's algorithm, reproducing GNU tsort's exact tie-break so the output is
    // byte-identical (not merely a valid ordering): (1) the initial zero-in-degree roots
    // are queued in sorted name order — GNU walks its node tree in order; (2) a FIFO queue;
    // (3) each popped node's successors are relaxed in their stored order (the reverse of
    // input appearance, since edges are prepended), appending any newly-freed node to the
    // tail. If fewer than every node is emitted, the rest form a cycle.
    int qh = 0, qt = 0;
    for (int n = 0; n < nnodes; n++) if (indeg[n] == 0) q[qt++] = n;
    for (int a = 1; a < qt; a++) {                        // insertion-sort the initial roots
        int v = q[a], b = a - 1;
        while (b >= 0 && ts_strcmp(names[q[b]], names[v]) > 0) { q[b + 1] = q[b]; b--; }
        q[b + 1] = v;
    }
    int out = 0;
    while (qh < qt) {
        int pick = q[qh++];
        uint32_t nl = 0; while (names[pick][nl]) nl++;
        emit(names[pick], nl, ctx);
        out++;
        for (int e = adj_head[pick]; e != -1; e = edge_next[e]) {
            int v = edge_to[e];
            if (indeg[v] > 0 && --indeg[v] == 0 && qt < TS_MAX_NODES) q[qt++] = v;
        }
    }
    return (out == nnodes) ? out : -2;
}
