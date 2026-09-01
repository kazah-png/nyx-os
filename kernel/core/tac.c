#include "tac.h"

// Does the separator sep[0..seplen) occur at text[i]? (bounded, no memcmp dependency)
static int sep_at(const char* t, uint32_t len, uint32_t i, const char* sep, uint32_t seplen) {
    if (i + seplen > len) return 0;
    for (uint32_t k = 0; k < seplen; k++) if (t[i + k] != sep[k]) return 0;
    return 1;
}

void tac_run(const char* text, uint32_t len, const char* sep, uint32_t seplen,
             uint32_t* starts, uint32_t maxrecs, tac_emit_fn emit, void* ctx) {
    if (len == 0) return;
    if (seplen == 0 || maxrecs == 0) { emit(text, len, ctx); return; }

    // Record starts: 0, then the byte after each separator occurrence (unless that
    // byte is the end of input — a trailing separator does not open a new record).
    uint32_t n = 0;
    starts[n++] = 0;
    uint32_t i = 0;
    while (i + seplen <= len) {
        if (sep_at(text, len, i, sep, seplen)) {
            uint32_t recend = i + seplen;                 // this record ends here (sep included)
            if (recend < len && n < maxrecs) starts[n++] = recend;
            i = recend;                                   // resume scanning after the separator
        } else {
            i++;
        }
    }

    // Emit records last-to-first; record k spans [starts[k], starts[k+1]) (or ..len).
    for (int k = (int)n - 1; k >= 0; k--) {
        uint32_t s = starts[k];
        uint32_t e = (k + 1 < (int)n) ? starts[k + 1] : len;
        emit(text + s, e - s, ctx);
    }
}

// ---- known-answer self-test (`tac`) ------------------------------------------------------
// A recording emit: concatenate the reversed record spans into a bounded buffer.
typedef struct { char* buf; uint32_t len; uint32_t cap; } tac_rec_t;
static void tac_rec_emit(const char* data, uint32_t len, void* ctx) {
    tac_rec_t* r = (tac_rec_t*)ctx;
    for (uint32_t i = 0; i < len && r->len < r->cap - 1; i++) r->buf[r->len++] = data[i];
    r->buf[r->len] = '\0';
}
static int tac_expect(const char* in, const char* sep, const char* want) {
    char out[256]; tac_rec_t r; r.buf = out; r.cap = (uint32_t)sizeof out; r.len = 0; out[0] = '\0';
    uint32_t starts[128], slen = 0; while (sep[slen]) slen++;
    tac_run(in, (uint32_t)strlen(in), sep, slen, starts, 128, tac_rec_emit, &r);
    return strcmp(out, want) == 0;
}
// Pins GNU-tac parity: trailing-separator reverse, the no-trailing-newline case (the last two
// lines JOIN — the subtle "separator = terminator" edge), a single line, two empty lines, and a
// multi-character separator both with and without a trailing separator.
int tac_selftest(void) {
    if (!tac_expect("a\nb\nc\n", "\n", "c\nb\na\n")) return 1;
    if (!tac_expect("a\nb\nc",   "\n", "cb\na\n"))   return 2;
    if (!tac_expect("only\n",    "\n", "only\n"))    return 3;
    if (!tac_expect("\n\n",      "\n", "\n\n"))      return 4;
    if (!tac_expect("aXXbXXc",   "XX", "cbXXaXX"))   return 5;
    if (!tac_expect("aXXbXX",    "XX", "bXXaXX"))    return 6;
    return 0;
}
