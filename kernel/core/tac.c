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
