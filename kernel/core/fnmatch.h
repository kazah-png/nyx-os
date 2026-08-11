#ifndef FNMATCH_H
#define FNMATCH_H
// Hardened shell-style glob matcher — one audited pattern engine for the untrusted
// patterns a shell/find would match filenames against. The naive recursive matcher
// (still living in user/sh.c's glob_match) recurses once per '*' expansion, so a
// pattern like "*a*a*a*a*b" against "aaaa...aaa" branches EXPONENTIALLY — a hostile
// name or pattern can hang the matcher (an algorithmic-complexity DoS), and deep
// recursion can also blow the stack. This engine walks pattern and text with a
// single remembered '*' backtrack point instead: O(n*m) worst case, no exponential
// blowup, and no recursion at all. It also adds POSIX character classes the ad-hoc
// matcher lacks: '[abc]', ranges '[a-z]', negation '[!...]' / '[^...]', and '\'
// escaping of the metacharacters.
#include "kernel.h"

// Match str against glob pattern pat. Metacharacters:
//   '*'        matches any run of characters, including the empty run
//   '?'        matches exactly one character
//   '[...]'    a character class; '[!...]'/'[^...]' negates; 'a-z' is a range;
//              a ']' as the first member is literal; an unterminated '[' with no
//              closing ']' is treated as a literal '[' (so a stray '[' never
//              over-reads past the string). '\' escapes the next character.
// Returns 1 on match, 0 on no match. NUL-safe: never reads past either string.
int glob_match(const char* pat, const char* str);

// Known-answer test (`glob`): literals, '*'/'?', character classes/ranges/negation,
// escaping, empty-string edges, and the catastrophic-backtracking pattern that must
// terminate quickly (the test completing at all is the proof it does not blow up).
int glob_selftest(void);

#endif // FNMATCH_H
