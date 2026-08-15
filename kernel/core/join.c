#include "join.h"

#define JOIN_MAXLINES 512
#define JOIN_RECCAP   8192

typedef struct { const char* p; uint32_t len; } jline_t;

static int join_blank(char c) { return c == ' ' || c == '\t'; }

// Locate the idx-th (0-based) field of s[0..len) under `delim`. Default (delim==0): fields are
// runs of non-blanks with leading/among blanks skipped. Otherwise: single-char `delim` splits,
// with no run-collapsing (so "a,,b" has an empty middle field). Returns 1 and sets *fs/*fl, or 0
// if idx is past the last field.
static int join_field_at(const char* s, uint32_t len, char delim, uint32_t idx,
                         uint32_t* fs, uint32_t* fl) {
    if (delim == 0) {
        uint32_t i = 0;
        for (uint32_t f = 0; ; f++) {
            while (i < len && join_blank(s[i])) i++;
            if (i >= len) return 0;
            uint32_t start = i;
            while (i < len && !join_blank(s[i])) i++;
            if (f == idx) { *fs = start; *fl = i - start; return 1; }
        }
    } else {
        uint32_t i = 0;
        for (uint32_t f = 0; ; f++) {
            uint32_t start = i;
            while (i < len && s[i] != delim) i++;
            if (f == idx) { *fs = start; *fl = i - start; return 1; }
            if (i >= len) return 0;        // idx is past the final field
            i++;                            // step over the delimiter
        }
    }
}

// The join key of a line = its join field, or the empty string if the line has too few fields.
static void join_key_of(const jline_t* ln, char delim, uint32_t jf,
                        const char** ks, uint32_t* kl) {
    uint32_t fs, fl;
    if (join_field_at(ln->p, ln->len, delim, jf - 1, &fs, &fl)) { *ks = ln->p + fs; *kl = fl; }
    else { *ks = ln->p + ln->len; *kl = 0; }
}

static int join_keycmp(const char* a, uint32_t al, const char* b, uint32_t bl) {
    uint32_t n = al < bl ? al : bl;
    for (uint32_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return al == bl ? 0 : (al < bl ? -1 : 1);
}

// Append a line's fields EXCEPT its join field, each preceded by the output separator.
static void join_append_rest(char* rec, uint32_t* o, const jline_t* ln, char delim,
                             uint32_t jf, char osep) {
    for (uint32_t idx = 0; ; idx++) {
        uint32_t fs, fl;
        if (!join_field_at(ln->p, ln->len, delim, idx, &fs, &fl)) break;
        if (idx == jf - 1) continue;
        if (*o < JOIN_RECCAP) rec[(*o)++] = osep;
        for (uint32_t k = 0; k < fl && *o < JOIN_RECCAP; k++) rec[(*o)++] = ln->p[fs + k];
    }
}

// Split text into lines (newline-delimited, newline excluded); returns the count (bounded).
static uint32_t join_split(const char* t, uint32_t len, jline_t* out) {
    uint32_t n = 0, i = 0;
    while (i < len && n < JOIN_MAXLINES) {
        uint32_t start = i;
        while (i < len && t[i] != '\n') i++;
        out[n].p = t + start; out[n].len = i - start; n++;
        if (i < len) i++;
    }
    return n;
}

void join_run(const char* t1, uint32_t l1, const char* t2, uint32_t l2,
              const join_opts_t* o, join_emit_fn emit, void* ctx) {
    static jline_t L1[JOIN_MAXLINES], L2[JOIN_MAXLINES];
    static char rec[JOIN_RECCAP];
    char osep = o->delim ? o->delim : ' ';
    uint32_t n1 = join_split(t1, l1, L1), n2 = join_split(t2, l2, L2);
    uint32_t i = 0, j = 0;

    while (i < n1 && j < n2) {
        const char *k1, *k2; uint32_t kl1, kl2;
        join_key_of(&L1[i], o->delim, o->jf1, &k1, &kl1);
        join_key_of(&L2[j], o->delim, o->jf2, &k2, &kl2);
        int c = join_keycmp(k1, kl1, k2, kl2);
        if (c < 0) {
            if (o->a1) { uint32_t x = 0; for (uint32_t z = 0; z < kl1; z++) rec[x++] = k1[z];
                         join_append_rest(rec, &x, &L1[i], o->delim, o->jf1, osep); emit(ctx, rec, x); }
            i++;
        } else if (c > 0) {
            if (o->a2) { uint32_t x = 0; for (uint32_t z = 0; z < kl2; z++) rec[x++] = k2[z];
                         join_append_rest(rec, &x, &L2[j], o->delim, o->jf2, osep); emit(ctx, rec, x); }
            j++;
        } else {
            // gather the runs of equal keys in both files, then emit the cartesian product
            uint32_t i2 = i;
            for (; i2 < n1; i2++) { const char* kk; uint32_t kn;
                join_key_of(&L1[i2], o->delim, o->jf1, &kk, &kn);
                if (join_keycmp(kk, kn, k1, kl1) != 0) break; }
            uint32_t j2 = j;
            for (; j2 < n2; j2++) { const char* kk; uint32_t kn;
                join_key_of(&L2[j2], o->delim, o->jf2, &kk, &kn);
                if (join_keycmp(kk, kn, k2, kl2) != 0) break; }
            for (uint32_t a = i; a < i2; a++)
                for (uint32_t b = j; b < j2; b++) {
                    uint32_t x = 0;
                    for (uint32_t z = 0; z < kl1; z++) rec[x++] = k1[z];        // the join field
                    join_append_rest(rec, &x, &L1[a], o->delim, o->jf1, osep);  // file1 remainder
                    join_append_rest(rec, &x, &L2[b], o->delim, o->jf2, osep);  // file2 remainder
                    emit(ctx, rec, x);
                }
            i = i2; j = j2;
        }
    }
    if (o->a1) for (; i < n1; i++) { const char* k; uint32_t kn; join_key_of(&L1[i], o->delim, o->jf1, &k, &kn);
        uint32_t x = 0; for (uint32_t z = 0; z < kn; z++) rec[x++] = k[z];
        join_append_rest(rec, &x, &L1[i], o->delim, o->jf1, osep); emit(ctx, rec, x); }
    if (o->a2) for (; j < n2; j++) { const char* k; uint32_t kn; join_key_of(&L2[j], o->delim, o->jf2, &k, &kn);
        uint32_t x = 0; for (uint32_t z = 0; z < kn; z++) rec[x++] = k[z];
        join_append_rest(rec, &x, &L2[j], o->delim, o->jf2, osep); emit(ctx, rec, x); }
}
