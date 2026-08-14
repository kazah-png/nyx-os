#ifndef NYX_CUT_H
#define NYX_CUT_H
// cut — print selected fields, characters, or bytes of each line, byte-for-byte with GNU
// cut. The pure line logic (parse a LIST like "1,3-5,7-" and filter one line) lives here so
// it can be unit-tested off-target; cmd_cut only does the file I/O and calls these. A LIST is
// a set of 1-based positions built from comma-separated ranges: "N" one position, "N-M" the
// inclusive range, "N-" from N to end, "-M" the same as "1-M". Selected positions are always
// emitted in increasing order and never duplicated, exactly as GNU cut does, so the order the
// ranges are written in never matters.
#include "kernel.h"

#define CUT_MAXPOS 1024   // highest explicit 1-based position kept in the bitmap; "N-" open
                          // ranges are unbounded and handled separately, so common uses like
                          // "cut -f2-" have no ceiling.

typedef enum { CUT_FIELDS = 0, CUT_CHARS = 1, CUT_BYTES = 2 } cut_mode_t;

typedef struct {
    cut_mode_t mode;                 // -f fields / -c chars / -b bytes (chars==bytes for ASCII)
    char       delim;                // field delimiter (field mode); GNU default is TAB
    char       odelim;               // output delimiter joining selected fields (= delim)
    int        suppress;             // -s: field mode, drop a line that has no delimiter
    int        any;                  // at least one valid range parsed
    uint32_t   open_from;            // >0 => also select every position >= open_from ("N-")
    uint8_t    sel[CUT_MAXPOS + 1];  // sel[p] != 0 => 1-based position p is selected
} cut_spec_t;

// cut_line returns this to mean "suppress the whole line" (emit nothing, not even a newline).
#define CUT_SKIP 0xFFFFFFFFu

// Parse a LIST into spec->sel / spec->open_from (the caller zero-inits spec and sets mode/
// delim/odelim/suppress first). Returns 0 on success, -1 on a malformed list (empty, a bare
// "-", a zero position, a decreasing "N-M", or a stray non-digit character).
int cut_parse_list(const char* list, cut_spec_t* spec);

// Process ONE line (bytes [line, line+len), newline already stripped) into out[0..outcap).
// Returns the output byte count, or CUT_SKIP to drop the line. In field mode a line with no
// delimiter is passed through unchanged (GNU behaviour), unless suppress is set.
uint32_t cut_line(const cut_spec_t* spec, const char* line, uint32_t len,
                  char* out, uint32_t outcap);

#endif
