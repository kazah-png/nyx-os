#include "csv.h"

uint32_t csv_parse(const char* text, uint32_t len, char delim,
                   char* fieldbuf, uint32_t fieldcap,
                   csv_emit_fn emit, void* ctx) {
    if (fieldcap == 0) return 0;
    uint32_t i = 0, row = 0, records = 0;

    while (i < len) {
        // A record terminator at the very start of a record is a BLANK line, which is an
        // EMPTY record (zero fields) — matching Python's csv.reader, which yields [] here
        // rather than ['']. (A quoted empty field ""\n is NOT this case: '"' is not a
        // terminator, so it falls through to the field scanner and becomes one '' field.)
        if (text[i] == '\n' || text[i] == '\r') {
            if (text[i] == '\r') { i++; if (i < len && text[i] == '\n') i++; }
            else i++;
            emit(ctx, row, 0, "", 0, CSV_EMPTY_ROW | CSV_LAST_IN_ROW);
            records++; row++;
            continue;
        }

        // Parse the fields of one record. The loop always emits at least one field, and a
        // trailing delimiter loops once more to emit the final empty field (so "a,b," -> 3
        // fields, the last empty), again matching Python.
        uint32_t col = 0;
        int done = 0;
        while (!done) {
            uint32_t fi = 0;
            if (text[i] == '"') {
                i++;                                          // consume the opening quote
                while (i < len) {
                    char c = text[i];
                    if (c == '"') {
                        if (i + 1 < len && text[i + 1] == '"') {   // "" -> a literal "
                            if (fi < fieldcap - 1) fieldbuf[fi++] = '"';
                            i += 2;
                        } else { i++; break; }                // a lone " closes the field
                    } else {
                        if (fi < fieldcap - 1) fieldbuf[fi++] = c;   // quotes protect delim/CR/LF
                        i++;
                    }
                }
                // Any bytes after the closing quote but before the next delimiter/terminator
                // are appended verbatim (permissive, like Python): e.g. "ab"cd -> abcd.
                while (i < len && text[i] != delim && text[i] != '\n' && text[i] != '\r') {
                    if (fi < fieldcap - 1) fieldbuf[fi++] = text[i];
                    i++;
                }
            } else {
                // Unquoted field: everything up to the next delimiter or record terminator.
                while (i < len && text[i] != delim && text[i] != '\n' && text[i] != '\r') {
                    if (fi < fieldcap - 1) fieldbuf[fi++] = text[i];
                    i++;
                }
            }
            fieldbuf[fi] = '\0';

            if (i < len && text[i] == delim) {
                i++;                                          // a further field follows
                emit(ctx, row, col, fieldbuf, fi, 0);
                col++;
            } else {
                emit(ctx, row, col, fieldbuf, fi, CSV_LAST_IN_ROW);
                if (i < len && text[i] == '\r') { i++; if (i < len && text[i] == '\n') i++; }
                else if (i < len && text[i] == '\n') i++;
                done = 1;                                     // record terminator or EOF
            }
        }
        records++; row++;
    }
    return records;
}

// ---- KAT ---------------------------------------------------------------------------
// Each vector is parsed into a flat, unambiguous string: every field is wrapped as
// <content>, records are separated by a real '\n', a blank record contributes no <...>
// marker (so [] and [''] differ: "" vs "<>"), and any embedded CR/LF inside a field is
// shown escaped (\r, \n) so it can't be confused with the record separator.
struct csv_kat_ctx { char* out; uint32_t cap; uint32_t n; int last_row; };

static void csv_kat_putc(struct csv_kat_ctx* c, char ch) {
    if (c->n + 1 < c->cap) c->out[c->n++] = ch;
}

static void csv_kat_emit(void* ctx, uint32_t row, uint32_t col,
                         const char* f, uint32_t len, uint32_t flags) {
    (void)col;
    struct csv_kat_ctx* c = (struct csv_kat_ctx*)ctx;
    if ((int)row != c->last_row) {
        if (c->last_row >= 0) csv_kat_putc(c, '\n');          // separate records
        c->last_row = (int)row;
    }
    if (flags & CSV_EMPTY_ROW) return;                        // blank line: emit no marker
    csv_kat_putc(c, '<');
    for (uint32_t i = 0; i < len; i++) {
        char b = f[i];
        if (b == '\n') { csv_kat_putc(c, '\\'); csv_kat_putc(c, 'n'); }
        else if (b == '\r') { csv_kat_putc(c, '\\'); csv_kat_putc(c, 'r'); }
        else csv_kat_putc(c, b);
    }
    csv_kat_putc(c, '>');
}

static int csv_kat_one(const char* in, const char* expect) {
    uint32_t n = 0; while (in[n]) n++;                        // no vector contains a NUL
    char field[128];
    char out[256];
    struct csv_kat_ctx c;
    c.out = out; c.cap = sizeof(out); c.n = 0; c.last_row = -1;
    csv_parse(in, n, ',', field, sizeof(field), csv_kat_emit, &c);
    out[c.n] = '\0';
    return strcmp(out, expect) == 0;
}

int csv_selftest(void) {
    if (!csv_kat_one("a,b,c\n",                   "<a><b><c>"))          return 1;  // plain
    if (!csv_kat_one("\"a,b\",c\n",               "<a,b><c>"))           return 2;  // quoted delim
    if (!csv_kat_one("\"she said \"\"hi\"\"\",x\n","<she said \"hi\"><x>")) return 3; // "" -> "
    if (!csv_kat_one("a,,c\n",                    "<a><><c>"))           return 4;  // empty middle
    if (!csv_kat_one("\"l1\nl2\",b\n",            "<l1\\nl2><b>"))       return 5;  // embedded LF
    if (!csv_kat_one("a,b,\n",                    "<a><b><>"))           return 6;  // trailing delim
    if (!csv_kat_one("a,b",                       "<a><b>"))             return 7;  // no final NL
    if (!csv_kat_one("a\n\nb\n",                  "<a>\n\n<b>"))         return 8;  // blank => []
    if (!csv_kat_one("a,b\r\nc,d\r\n",            "<a><b>\n<c><d>"))     return 9;  // CRLF records
    if (!csv_kat_one(",\n",                       "<><>"))               return 10; // two empties
    if (!csv_kat_one("\"abc",                     "<abc>"))              return 11; // unterminated
    if (!csv_kat_one("",                          ""))                   return 12; // empty input
    return 0;
}
