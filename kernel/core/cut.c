#include "cut.h"

// Is the 1-based position p selected by this spec? A position is selected if it is set in the
// explicit bitmap OR falls within an open-ended "N-" range.
static int cut_selected(const cut_spec_t* s, uint32_t p) {
    if (p == 0) return 0;
    if (s->open_from && p >= s->open_from) return 1;
    if (p <= CUT_MAXPOS && s->sel[p]) return 1;
    for (int i = 0; i < s->n_hi; i++)                  // closed ranges past the bitmap ceiling
        if (p >= s->hi_lo[i] && p <= s->hi_hi[i]) return 1;
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
            uint32_t bm = (b > CUT_MAXPOS) ? CUT_MAXPOS : b;   // fill the bitmap up to the ceiling
            for (uint32_t q = a; q <= bm; q++) spec->sel[q] = 1;
            if (b > CUT_MAXPOS) {                              // keep the exact closed range past
                if (spec->n_hi >= CUT_MAXHI) return -1;        // the ceiling (NOT open-ended)
                spec->hi_lo[spec->n_hi] = a; spec->hi_hi[spec->n_hi] = b; spec->n_hi++;
            }
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

// KAT for the pure cut core: LIST parsing (valid + every malformed form), field extraction
// with explicit and open-ended ranges, the GNU no-delimiter passthrough, the -s suppress
// (CUT_SKIP), and character mode. Returns 0 on pass, else the failing case number.
static int cut_streq(const char* out, uint32_t n, const char* want) {
    uint32_t wl = 0; while (want[wl]) wl++;
    if (n != wl) return 0;
    for (uint32_t i = 0; i < n; i++) if (out[i] != want[i]) return 0;
    return 1;
}
int cut_selftest(void) {
    cut_spec_t s, e; char out[64]; uint32_t n;
    // LIST parsing: a valid list sets the right bitmap + open range...
    memset_asm(&s, 0, sizeof s);
    if (cut_parse_list("1,3-5,7-", &s) != 0) return 1;
    if (!s.sel[1] || s.sel[2] || !s.sel[3] || !s.sel[4] || !s.sel[5] || s.open_from != 7) return 2;
    // ...and every malformed form is rejected.
    memset_asm(&e, 0, sizeof e); if (cut_parse_list("",    &e) == 0) return 3;   // empty
    memset_asm(&e, 0, sizeof e); if (cut_parse_list("-",   &e) == 0) return 4;   // bare dash
    memset_asm(&e, 0, sizeof e); if (cut_parse_list("0",   &e) == 0) return 5;   // zero position
    memset_asm(&e, 0, sizeof e); if (cut_parse_list("5-3", &e) == 0) return 6;   // decreasing range
    memset_asm(&e, 0, sizeof e); if (cut_parse_list("2a",  &e) == 0) return 7;   // stray non-digit
    // Field mode: explicit fields joined by the output delimiter.
    memset_asm(&s, 0, sizeof s); s.mode = CUT_FIELDS; s.delim = ','; s.odelim = ',';
    if (cut_parse_list("1,3", &s) != 0) return 8;
    n = cut_line(&s, "a,b,c,d", 7, out, sizeof out); if (!cut_streq(out, n, "a,c")) return 9;
    // Open-ended "N-" range.
    memset_asm(&s, 0, sizeof s); s.mode = CUT_FIELDS; s.delim = ','; s.odelim = ',';
    if (cut_parse_list("2-", &s) != 0) return 10;
    n = cut_line(&s, "a,b,c,d", 7, out, sizeof out); if (!cut_streq(out, n, "b,c,d")) return 11;
    // A line with no delimiter passes through unchanged (GNU behaviour).
    n = cut_line(&s, "abc", 3, out, sizeof out); if (!cut_streq(out, n, "abc")) return 12;
    // -s suppresses a no-delimiter line entirely.
    memset_asm(&s, 0, sizeof s); s.mode = CUT_FIELDS; s.delim = ','; s.odelim = ','; s.suppress = 1;
    if (cut_parse_list("1", &s) != 0) return 13;
    if (cut_line(&s, "abc", 3, out, sizeof out) != CUT_SKIP) return 14;
    // Character mode: selected positions, no separator.
    memset_asm(&s, 0, sizeof s); s.mode = CUT_CHARS; s.delim = ','; s.odelim = ',';
    if (cut_parse_list("1,3-4", &s) != 0) return 15;
    n = cut_line(&s, "hello", 5, out, sizeof out); if (!cut_streq(out, n, "hll")) return 16;
    // A closed range past the bitmap ceiling (CUT_MAXPOS) is kept EXACTLY, not turned open-ended.
    memset_asm(&s, 0, sizeof s); s.mode = CUT_CHARS;
    if (cut_parse_list("1025-1027", &s) != 0) return 17;
    if (cut_selected(&s, 1024) || !cut_selected(&s, 1025) ||
        !cut_selected(&s, 1027) || cut_selected(&s, 1028)) return 18;
    // A single position past the ceiling selects only itself, not everything after it.
    memset_asm(&s, 0, sizeof s); s.mode = CUT_CHARS;
    if (cut_parse_list("2000", &s) != 0) return 19;
    if (!cut_selected(&s, 2000) || cut_selected(&s, 2001) || cut_selected(&s, 1999)) return 20;
    return 0;
}
