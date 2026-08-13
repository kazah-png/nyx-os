// ============================================================
// comm.c - the merge engine for the `comm` coreutil (see comm.h). Pure logic,
// bounded; assumes each input is sorted. Cross-checked byte-identical to GNU comm.
// ============================================================
#include "comm.h"

// Next line in [*p, end): sets *line/*len (excluding the '\n'), advances *p past the
// '\n'. Returns 1 if a line was produced, 0 at end. A final line without a trailing
// newline is still a line.
static int next_line(const char** p, const char* end, const char** line, int* len) {
    if (*p >= end) return 0;
    const char* s = *p; const char* q = s;
    while (q < end && *q != '\n') q++;
    *line = s; *len = (int)(q - s);
    *p = (q < end) ? q + 1 : q;
    return 1;
}

// Byte-wise (C-locale) line comparison: -1 / 0 / +1.
static int line_cmp(const char* a, int al, const char* b, int bl) {
    int n = al < bl ? al : bl;
    for (int i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return al == bl ? 0 : (al < bl ? -1 : 1);
}

void comm_run(const char* a, int alen, const char* b, int blen,
              int s1, int s2, int s3, comm_emit_fn emit, void* ctx) {
    const char* pa = a; const char* ea = a + alen;
    const char* pb = b; const char* eb = b + blen;
    const char *la, *lb; int lal, lbl;
    int tab2 = s1 ? 1 : 0;                 // tab stops before column 2 / column 3
    int tab3 = (s1 ? 1 : 0) + (s2 ? 1 : 0);

    int haveA = next_line(&pa, ea, &la, &lal);
    int haveB = next_line(&pb, eb, &lb, &lbl);
    while (haveA && haveB) {
        int c = line_cmp(la, lal, lb, lbl);
        if (c < 0)      { if (s1) emit(0,    la, lal, ctx); haveA = next_line(&pa, ea, &la, &lal); }
        else if (c > 0) { if (s2) emit(tab2, lb, lbl, ctx); haveB = next_line(&pb, eb, &lb, &lbl); }
        else            { if (s3) emit(tab3, la, lal, ctx);
                          haveA = next_line(&pa, ea, &la, &lal);
                          haveB = next_line(&pb, eb, &lb, &lbl); }
    }
    while (haveA) { if (s1) emit(0,    la, lal, ctx); haveA = next_line(&pa, ea, &la, &lal); }
    while (haveB) { if (s2) emit(tab2, lb, lbl, ctx); haveB = next_line(&pb, eb, &lb, &lbl); }
}
