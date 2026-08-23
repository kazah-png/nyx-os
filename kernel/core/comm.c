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

// ---- known-answer self-test (`comm`) ----------------------------------------------------
// A recording emit: append `tabs` tab stops, the line, then a newline into a bounded buffer.
typedef struct { char* buf; int len; int cap; } comm_rec_t;
static void comm_rec_emit(int tabs, const char* line, int len, void* ctx) {
    comm_rec_t* r = (comm_rec_t*)ctx;
    for (int t = 0; t < tabs && r->len < r->cap - 1; t++) r->buf[r->len++] = '\t';
    for (int i = 0; i < len && r->len < r->cap - 1; i++) r->buf[r->len++] = line[i];
    if (r->len < r->cap - 1) r->buf[r->len++] = '\n';
    r->buf[r->len] = '\0';
}
static int comm_seq(const char* s, const char* want) {
    int i = 0; while (s[i] && want[i]) { if (s[i] != want[i]) return 0; i++; }
    return s[i] == want[i];
}
// Pins the three-column merge, the tab-stop bookkeeping (a suppressed column drops its tab from
// later ones, GNU -1/-2/-3), the tail drains, and the no-trailing-newline final line.
int comm_selftest(void) {
    char out[256]; comm_rec_t r; r.buf = out; r.cap = (int)sizeof out;
    const char* A = "apple\nbanana\ncherry\n"; int al = (int)strlen(A);
    const char* B = "banana\ncherry\ndate\n";  int bl = (int)strlen(B);
    r.len = 0; out[0] = '\0'; comm_run(A, al, B, bl, 1, 1, 1, comm_rec_emit, &r);   // all 3 columns
    if (!comm_seq(out, "apple\n\t\tbanana\n\t\tcherry\n\tdate\n")) return 1;
    r.len = 0; out[0] = '\0'; comm_run(A, al, B, bl, 0, 0, 1, comm_rec_emit, &r);   // -12: common, no tabs
    if (!comm_seq(out, "banana\ncherry\n")) return 2;
    r.len = 0; out[0] = '\0'; comm_run(A, al, B, bl, 1, 0, 0, comm_rec_emit, &r);   // -23: only-in-A
    if (!comm_seq(out, "apple\n")) return 3;
    r.len = 0; out[0] = '\0'; comm_run(A, al, B, bl, 0, 1, 0, comm_rec_emit, &r);   // -13: only-in-B, no tab
    if (!comm_seq(out, "date\n")) return 4;
    { const char* a2 = "a\nb\nc\n"; const char* b2 = "a\n";                         // tail drain (A outruns B)
      r.len = 0; out[0] = '\0'; comm_run(a2, (int)strlen(a2), b2, (int)strlen(b2), 1, 1, 1, comm_rec_emit, &r);
      if (!comm_seq(out, "\t\ta\nb\nc\n")) return 5; }
    { const char* a4 = "m\nz"; const char* b4 = "z";                                // final line, no newline
      r.len = 0; out[0] = '\0'; comm_run(a4, (int)strlen(a4), b4, (int)strlen(b4), 1, 1, 1, comm_rec_emit, &r);
      if (!comm_seq(out, "m\n\t\tz\n")) return 6; }
    return 0;
}
