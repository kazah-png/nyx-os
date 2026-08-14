#ifndef NYX_CSV_H
#define NYX_CSV_H
// RFC 4180 CSV field parser. A small, pure, allocation-free state machine that splits an
// untrusted CSV buffer into records and fields, correctly handling double-quoted fields
// (a delimiter, CR, LF, or a doubled "" inside quotes is literal; a "" unescapes to one ").
// It is HARDENED for untrusted input: every field byte is bounds-checked into the caller's
// buffer (never overflowed), the scanner always advances (no infinite loop), and an
// unterminated quoted field runs safely to end of input instead of reading past it.
// Behaviour matches Python's csv.reader (default dialect), including the two subtle quirks
// a naive splitter gets wrong: a trailing delimiter yields a final EMPTY field, and a
// completely blank line yields an EMPTY record (zero fields) rather than one empty field.
#include "kernel.h"

#define CSV_LAST_IN_ROW  0x1u   // this field is the final field of its record
#define CSV_EMPTY_ROW    0x2u   // this record has NO fields (a blank line); field/len empty

// Called once per field, in order. `row` and `col` are 0-based indices. `field`/`len` is
// the UNESCAPED field value (a doubled "" has become a single "). A blank line calls emit
// exactly once with flags == (CSV_EMPTY_ROW | CSV_LAST_IN_ROW) and an empty field, so a
// consumer can tell an empty record apart from a record holding one empty field.
typedef void (*csv_emit_fn)(void* ctx, uint32_t row, uint32_t col,
                            const char* field, uint32_t len, uint32_t flags);

// Parse text[0..len) as RFC 4180 CSV using `delim` as the field separator (typically ',').
// Records end at LF, CRLF, or a lone CR outside quotes, or at end of input. Each unescaped
// field is built into fieldbuf[0..fieldcap) (NUL-terminated, truncated if it would not fit,
// never overflowed) and handed to `emit`. Returns the number of records emitted.
uint32_t csv_parse(const char* text, uint32_t len, char delim,
                   char* fieldbuf, uint32_t fieldcap,
                   csv_emit_fn emit, void* ctx);

int csv_selftest(void);   // 0 = pass; returns the failing vector's index otherwise
#endif
