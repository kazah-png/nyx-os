#ifndef NYX_WC_H
#define NYX_WC_H
#include "types.h"

// Count lines, words, byte count, and the longest-line length of buf[0..len). A "line" ends
// at '\n'; a "word" is a maximal run of non-whitespace (space/tab/newline/CR separate). The
// longest-line length (GNU `wc -L`) expands tabs to the next 8-column stop and counts a final
// line with no trailing newline. Any out-pointer may be NULL. Pure — no I/O; shared by the
// `wc` shell builtin and its self-test.
void wc_count(const char* buf, int len, int* lines, int* words, int* chars, int* max_len);

int wc_selftest(void);   // known-answer test of wc_count

#endif // NYX_WC_H
