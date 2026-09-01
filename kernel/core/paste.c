#include "paste.h"

// Pull the next '\n'-delimited line out of text[*pos..len). Returns 1 and sets
// *line/*linelen (the line WITHOUT its trailing newline) advancing *pos past the
// newline; returns 0 when the cursor is already at the end (file exhausted).
static int next_line(const char* text, uint32_t len, uint32_t* pos,
                     const char** line, uint32_t* linelen) {
    if (*pos >= len) return 0;
    uint32_t s = *pos, e = s;
    while (e < len && text[e] != '\n') e++;
    *line = text + s;
    *linelen = e - s;
    *pos = (e < len) ? e + 1 : e;          // skip the '\n'; last line may have none
    return 1;
}

void paste_run(const paste_file_t* files, int nfiles,
               const char* delims, int ndelims, int serial,
               paste_emit_fn emit, void* ctx) {
    if (nfiles <= 0 || ndelims <= 0) return;

    if (serial) {
        for (int f = 0; f < nfiles; f++) {
            uint32_t p = 0; int col = 0;
            const char* line; uint32_t ll;
            while (next_line(files[f].text, files[f].len, &p, &line, &ll)) {
                if (col > 0) emit(delims[(col - 1) % ndelims], ctx);
                for (uint32_t k = 0; k < ll; k++) emit(line[k], ctx);
                col++;
            }
            emit('\n', ctx);               // one output line per file (empty if no lines)
        }
        return;
    }

    // Parallel: a cursor per file (caller bounds nfiles; 64 is far above any real use).
    uint32_t pos[64];
    if (nfiles > 64) nfiles = 64;
    for (int f = 0; f < nfiles; f++) pos[f] = 0;
    for (;;) {
        int any = 0;
        for (int f = 0; f < nfiles; f++) if (pos[f] < files[f].len) { any = 1; break; }
        if (!any) break;                   // every file exhausted -> done
        for (int f = 0; f < nfiles; f++) {
            if (f > 0) emit(delims[(f - 1) % ndelims], ctx);
            const char* line; uint32_t ll;
            if (next_line(files[f].text, files[f].len, &pos[f], &line, &ll))
                for (uint32_t k = 0; k < ll; k++) emit(line[k], ctx);
            // an exhausted file contributes an empty field
        }
        emit('\n', ctx);
    }
}

// ---- known-answer self-test (`paste`) ----------------------------------------------------
typedef struct { char* buf; uint32_t n; uint32_t cap; } paste_rec_t;
static void paste_rec_emit(char c, void* ctx) {
    paste_rec_t* r = (paste_rec_t*)ctx;
    if (r->n < r->cap - 1) r->buf[r->n++] = c;
    r->buf[r->n] = '\0';
}
static int paste_expect(const paste_file_t* files, int nf, const char* delims, int nd,
                        int serial, const char* want) {
    char out[256]; paste_rec_t r; r.buf = out; r.cap = (uint32_t)sizeof out; r.n = 0; out[0] = '\0';
    paste_run(files, nf, delims, nd, serial, paste_rec_emit, &r);
    return strcmp(out, want) == 0;
}
// Pins GNU-paste parity: parallel merge of equal files, unequal lengths (a spent file gives an
// empty field), serial join of one file's lines, multi-char delimiter CYCLING in both modes,
// and a final line with no trailing newline.
int paste_selftest(void) {
    { paste_file_t f[2] = {{"a\nb\n",4},{"1\n2\n",4}};      if (!paste_expect(f,2,"\t",1,0,"a\t1\nb\t2\n"))     return 1; }
    { paste_file_t f[2] = {{"a\nb\nc\n",6},{"1\n",2}};      if (!paste_expect(f,2,"\t",1,0,"a\t1\nb\t\nc\t\n")) return 2; }
    { paste_file_t f[1] = {{"a\nb\nc\n",6}};                if (!paste_expect(f,1,"\t",1,1,"a\tb\tc\n"))        return 3; }
    { paste_file_t f[3] = {{"a\n",2},{"b\n",2},{"c\n",2}};  if (!paste_expect(f,3,"-+",2,0,"a-b+c\n"))          return 4; }
    { paste_file_t f[2] = {{"a\nb",3},{"1\n2",3}};          if (!paste_expect(f,2,"\t",1,0,"a\t1\nb\t2\n"))     return 5; }
    { paste_file_t f[1] = {{"w\nx\ny\nz\n",8}};             if (!paste_expect(f,1,"-+",2,1,"w-x+y-z\n"))        return 6; }
    return 0;
}
