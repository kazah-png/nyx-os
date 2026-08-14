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
