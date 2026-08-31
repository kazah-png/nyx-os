#include "uniq.h"

static int uniq_blank(char c) { return c == ' ' || c == '\t'; }

// Offset into a line where the comparison key starts: skip -f fields (each = leading blanks
// then non-blanks), then skip -s characters. Mirrors GNU uniq's find_field().
static uint32_t uniq_key_start(const uniq_opts_t* o, const char* line, uint32_t len) {
    uint32_t i = 0;
    for (uint32_t f = 0; f < o->skip_fields && i < len; f++) {
        while (i < len && uniq_blank(line[i])) i++;
        while (i < len && !uniq_blank(line[i])) i++;
    }
    for (uint32_t c = 0; c < o->skip_chars && i < len; c++) i++;
    return i;
}

// Do two lines share a comparison key? Keys are the post-skip substrings, each clamped to
// -w characters, compared byte-for-byte (case-folded when -i). Matches GNU uniq's different().
static int uniq_key_equal(const uniq_opts_t* o, const char* a, uint32_t alen,
                          const char* b, uint32_t blen) {
    uint32_t ai = uniq_key_start(o, a, alen), bi = uniq_key_start(o, b, blen);
    uint32_t an = alen - ai, bn = blen - bi;
    if (o->check_chars_set) {
        if (an > o->check_chars) an = o->check_chars;
        if (bn > o->check_chars) bn = o->check_chars;
    }
    if (an != bn) return 0;
    for (uint32_t k = 0; k < an; k++) {
        char ca = a[ai + k], cb = b[bi + k];
        if (o->ignore_case) {
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        }
        if (ca != cb) return 0;
    }
    return 1;
}

// Emit one finished run (its first line + run length) if the flags call for it.
static void uniq_flush(const uniq_opts_t* o, const char* line, uint32_t linelen,
                       uint32_t count, uniq_emit_fn emit, void* ctx) {
    if (o->only_dup && o->only_uniq) return;         // -d -u together prints nothing
    if (o->only_dup && count < 2) return;
    if (o->only_uniq && count != 1) return;

    static char rec[4200];
    uint32_t n = 0;
    if (o->count) {                                   // "%7u " count prefix
        char num[16]; int ni = 0; uint32_t c = count;
        if (c == 0) num[ni++] = '0';
        while (c) { num[ni++] = (char)('0' + c % 10); c /= 10; }
        for (int p = 0; p < 7 - ni; p++) rec[n++] = ' ';   // right-justify in a 7-wide field
        for (int d = ni - 1; d >= 0; d--) rec[n++] = num[d];
        rec[n++] = ' ';
    }
    for (uint32_t k = 0; k < linelen && n < sizeof(rec); k++) rec[n++] = line[k];
    emit(ctx, rec, n);
}

void uniq_run(const uniq_opts_t* o, const char* text, uint32_t len,
              uniq_emit_fn emit, void* ctx) {
    const char* run_line = 0; uint32_t run_len = 0, run_count = 0;
    int have_run = 0;
    uint32_t i = 0;
    while (i < len) {
        uint32_t start = i;
        while (i < len && text[i] != '\n') i++;
        uint32_t linelen = i - start;
        const char* line = text + start;
        if (i < len) i++;                             // consume the newline

        if (!have_run) {
            run_line = line; run_len = linelen; run_count = 1; have_run = 1;
        } else if (uniq_key_equal(o, run_line, run_len, line, linelen)) {
            run_count++;
        } else {
            uniq_flush(o, run_line, run_len, run_count, emit, ctx);
            run_line = line; run_len = linelen; run_count = 1;
        }
    }
    if (have_run) uniq_flush(o, run_line, run_len, run_count, emit, ctx);
}

// ---- known-answer self-test (`uniq`) ----------------------------------------------------
// A recording emit: append the record bytes then a newline into a bounded buffer (like join's).
typedef struct { char* buf; uint32_t len; uint32_t cap; } uniq_rec_t;
static void uniq_rec_emit(void* ctx, const char* out, uint32_t len) {
    uniq_rec_t* r = (uniq_rec_t*)ctx;
    for (uint32_t i = 0; i < len && r->len < r->cap - 1; i++) r->buf[r->len++] = out[i];
    if (r->len < r->cap - 1) r->buf[r->len++] = '\n';
    r->buf[r->len] = '\0';
}
// Run uniq over `in` with options `o`; return 1 iff the emitted stream equals `want`.
static int uniq_expect(const uniq_opts_t* o, const char* in, const char* want) {
    char out[256]; uniq_rec_t r; r.buf = out; r.cap = (uint32_t)sizeof out; r.len = 0; out[0] = '\0';
    uniq_run(o, in, (uint32_t)strlen(in), uniq_rec_emit, &r);
    return strcmp(out, want) == 0;
}
// Pins GNU-uniq parity across the whole flag matrix: bare adjacent-fold, -c counts (7-wide),
// -d / -u run selection, -d -u together (prints nothing), -i case-fold (first line kept),
// -f field / -s char / -w width key extraction, -w 0 (all lines fold), and a last line with
// no trailing newline. uniq_run is pure, so a refactor that broke any of these would be caught.
int uniq_selftest(void) {
    { uniq_opts_t o = {0};                                                     // 1) bare adjacent-fold
      if (!uniq_expect(&o, "a\na\nb\nb\nb\nc\n", "a\nb\nc\n")) return 1; }
    { uniq_opts_t o = {0}; o.count = 1;                                         // 2) -c occurrence count
      if (!uniq_expect(&o, "a\na\nb\n", "      2 a\n      1 b\n")) return 2; }
    { uniq_opts_t o = {0}; o.only_dup = 1;                                      // 3) -d only repeated runs
      if (!uniq_expect(&o, "a\na\nb\nc\nc\n", "a\nc\n")) return 3; }
    { uniq_opts_t o = {0}; o.only_uniq = 1;                                     // 4) -u only singletons
      if (!uniq_expect(&o, "a\na\nb\nc\nc\n", "b\n")) return 4; }
    { uniq_opts_t o = {0}; o.only_dup = 1; o.only_uniq = 1;                     // 5) -d -u -> nothing
      if (!uniq_expect(&o, "a\na\nb\n", "")) return 5; }
    { uniq_opts_t o = {0}; o.ignore_case = 1;                                   // 6) -i (first line kept)
      if (!uniq_expect(&o, "Foo\nfoo\nFOO\nbar\n", "Foo\nbar\n")) return 6; }
    { uniq_opts_t o = {0}; o.skip_fields = 1;                                   // 7) -f 1 skip a field
      if (!uniq_expect(&o, "1 apple\n2 apple\n3 pear\n", "1 apple\n3 pear\n")) return 7; }
    { uniq_opts_t o = {0}; o.skip_chars = 1;                                    // 8) -s 1 skip a char
      if (!uniq_expect(&o, "Xapple\nYapple\nZpear\n", "Xapple\nZpear\n")) return 8; }
    { uniq_opts_t o = {0}; o.check_chars = 5; o.check_chars_set = 1;            // 9) -w 5 clamp the key
      if (!uniq_expect(&o, "apple1\napple2\ngrape1\n", "apple1\ngrape1\n")) return 9; }
    { uniq_opts_t o = {0}; o.check_chars = 0; o.check_chars_set = 1;            // 10) -w 0 -> every line folds
      if (!uniq_expect(&o, "ab\ncd\nef\n", "ab\n")) return 10; }
    { uniq_opts_t o = {0};                                                      // 11) last line, no newline
      if (!uniq_expect(&o, "a\na\nb", "a\nb\n")) return 11; }
    return 0;
}
