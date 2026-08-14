#include "cut.h"

// Is the 1-based position p selected by this spec? A position is selected if it is set in the
// explicit bitmap OR falls within an open-ended "N-" range.
static int cut_selected(const cut_spec_t* s, uint32_t p) {
    if (p == 0) return 0;
    if (s->open_from && p >= s->open_from) return 1;
    if (p <= CUT_MAXPOS && s->sel[p]) return 1;
    return 0;
}

int cut_parse_list(const char* list, cut_spec_t* spec) {
    if (!list) return -1;
    const char* p = list;
    while (*p) {
        while (*p == ',') p++;                 // tolerate empty tokens (GNU ignores them)
        if (!*p) break;

        uint32_t lo = 0, hi = 0;
        int have_lo = 0, have_dash = 0, have_hi = 0;
        if (*p >= '0' && *p <= '9') {
            have_lo = 1;
            while (*p >= '0' && *p <= '9') {   // bounded so a huge literal can't overflow
                lo = lo * 10 + (uint32_t)(*p - '0'); p++;
                if (lo > 100000000u) return -1;
            }
        }
        if (*p == '-') {
            have_dash = 1; p++;
            if (*p >= '0' && *p <= '9') {
                have_hi = 1;
                while (*p >= '0' && *p <= '9') {
                    hi = hi * 10 + (uint32_t)(*p - '0'); p++;
                    if (hi > 100000000u) return -1;
                }
            }
        }
        if (*p && *p != ',') return -1;        // a token must end at a comma or end-of-list

        uint32_t rlo, rhi; int open = 0;
        if (have_dash) {
            if (!have_lo && !have_hi) return -1;               // a bare "-" is invalid
            rlo = have_lo ? lo : 1;                            // "-M" == "1-M"
            if (have_hi) rhi = hi; else { open = 1; rhi = 0; } // "N-" runs to end of line
        } else {
            if (!have_lo) return -1;
            rlo = rhi = lo;                                    // a single "N"
        }
        if (rlo == 0) return -1;                               // positions are numbered from 1
        if (!open && rhi < rlo) return -1;                     // a decreasing range is invalid

        if (open) {
            if (spec->open_from == 0 || rlo < spec->open_from) spec->open_from = rlo;
        } else {
            uint32_t a = rlo, b = rhi;
            if (b > CUT_MAXPOS) {                              // range past the bitmap ceiling:
                if (spec->open_from == 0 || a < spec->open_from) spec->open_from = a;  // treat
                b = CUT_MAXPOS;                                // its tail as open-ended
            }
            for (uint32_t q = a; q <= b; q++) spec->sel[q] = 1;
        }
        spec->any = 1;
    }
    return spec->any ? 0 : -1;
}

uint32_t cut_line(const cut_spec_t* spec, const char* line, uint32_t len,
                  char* out, uint32_t outcap) {
    uint32_t o = 0;

    if (spec->mode == CUT_FIELDS) {
        int has_delim = 0;
        for (uint32_t i = 0; i < len; i++)
            if (line[i] == spec->delim) { has_delim = 1; break; }
        if (!has_delim) {                        // GNU: a line without the delimiter is passed
            if (spec->suppress) return CUT_SKIP; // through verbatim, unless -s drops it
            for (uint32_t i = 0; i < len && o < outcap; i++) out[o++] = line[i];
            return o;
        }
        uint32_t field = 1, emitted = 0, fstart = 0;
        for (;;) {
            uint32_t fend = fstart;
            while (fend < len && line[fend] != spec->delim) fend++;
            if (cut_selected(spec, field)) {
                if (emitted && o < outcap) out[o++] = spec->odelim;
                for (uint32_t k = fstart; k < fend && o < outcap; k++) out[o++] = line[k];
                emitted = 1;
            }
            if (fend >= len) break;
            fstart = fend + 1; field++;
        }
        return o;
    }

    // Character / byte mode: copy the selected positions in order, no separator.
    for (uint32_t pos = 1; pos <= len; pos++)
        if (cut_selected(spec, pos) && o < outcap) out[o++] = line[pos - 1];
    return o;
}
