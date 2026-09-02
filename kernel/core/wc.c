#include "wc.h"

// Extracted verbatim from the `wc` builtin so the count is defined ONCE and can be unit-tested
// off the kernel stack. The three character rules and the tab-stop math match GNU wc:
//   - bytes  = len (raw, unaffected by content)
//   - words  = count of transitions into a non-separator run (space/tab/'\n'/'\r' separate)
//   - -L     = the widest line, tabs advancing cur to the next multiple of 8, and a final
//              unterminated line still measured.
void wc_count(const char* buf, int len, int* lines, int* words, int* chars, int* max_len) {
    int l = 0, w = 0, in_word = 0, cur = 0, mx = 0;
    if (len < 0) len = 0;
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n')      { l++; if (cur > mx) mx = cur; cur = 0; }
        else if (c == '\t') cur += 8 - (cur % 8);          // advance to the next 8-column tab stop
        else                cur++;
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') in_word = 0;
        else if (!in_word) { in_word = 1; w++; }
    }
    if (cur > mx) mx = cur;                                 // a final line with no trailing '\n'
    if (lines)   *lines = l;
    if (words)   *words = w;
    if (chars)   *chars = len;
    if (max_len) *max_len = mx;
}

// ---- known-answer self-test (`wc`) --------------------------------------------------------
// Returns 1 iff wc_count(s, |s|) yields exactly (l, w, c, L). |s| is taken as the C length,
// so embedded '\0' isn't exercised here (wc reads a byte range, never a C string, in practice).
static int wc_check(const char* s, int L_, int W_, int C_, int maxL_) {
    int len = 0; while (s[len]) len++;
    int l, w, c, mx;
    wc_count(s, len, &l, &w, &c, &mx);
    return l == L_ && w == W_ && c == C_ && mx == maxL_;
}

int wc_selftest(void) {
    // basic line: 11 printable chars + '\n'
    if (!wc_check("hello world\n", 1, 2, 12, 11)) return 1;
    // no trailing newline: 0 lines, the last (only) line still measured
    if (!wc_check("abc",           0, 1, 3, 3))   return 2;
    // empty input: all zero
    if (!wc_check("",              0, 0, 0, 0))   return 3;
    // tab expands to the next 8-col stop for -L: 'a'(1) '\t'(->8) 'b'(9) => max_len 9
    if (!wc_check("a\tb\n",        1, 2, 4, 9))   return 4;
    // runs of spaces separate words but count toward the line length
    if (!wc_check("  a  b  ",      0, 2, 8, 8))   return 5;
    // CR is a word separator and counts as a column; two lines
    if (!wc_check("a b\r\nc\n",    2, 3, 7, 4))   return 6;
    // several blank lines: 3 newlines, no words, longest line is 0
    if (!wc_check("\n\n\n",        3, 0, 3, 0))   return 7;
    // leading tab then text: '\t'(->8) 'x'(9) => max_len 9, one word
    if (!wc_check("\tx\n",         1, 1, 3, 9))   return 8;
    return 0;
}
