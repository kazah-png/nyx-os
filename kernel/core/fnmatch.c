// ============================================================
// fnmatch.c - hardened shell-style glob matcher (see fnmatch.h)
// ============================================================
#include "fnmatch.h"

// Match a '[...]' class at *pp (which points at '[') against char c. Returns 1/0
// for match/no-match and advances *pp past the closing ']'. Returns -1 for a
// malformed class (no closing ']' before the string end), leaving *pp untouched so
// the caller treats the '[' as an ordinary literal — this is what keeps a stray '['
// from ever scanning past the NUL.
static int match_class(const char** pp, char c) {
    const char* p = *pp + 1;                 // skip '['
    int negate = 0;
    if (*p == '!' || *p == '^') { negate = 1; p++; }
    int matched = 0;
    int first = 1;                           // a ']' in the first slot is a literal member
    while (*p && (*p != ']' || first)) {
        first = 0;
        char lo;
        if (*p == '\\' && p[1]) { lo = p[1]; p += 2; }   // escaped member
        else                   { lo = *p;   p += 1; }
        if (*p == '-' && p[1] && p[1] != ']') {          // range  lo-hi
            const char* q = p + 1;
            char hi;
            if (*q == '\\' && q[1]) { hi = q[1]; p = q + 2; }
            else                   { hi = *q;   p = q + 1; }
            if ((unsigned char)c >= (unsigned char)lo &&
                (unsigned char)c <= (unsigned char)hi) matched = 1;
        } else {
            if ((unsigned char)c == (unsigned char)lo) matched = 1;
        }
    }
    if (*p != ']') return -1;                // unterminated -> caller uses literal '['
    *pp = p + 1;                             // consume the ']'
    return negate ? !matched : matched;
}

int glob_match(const char* pat, const char* str) {
    const char* p = pat;
    const char* s = str;
    const char* star_p = 0;                  // pattern just after the last '*' seen
    const char* star_s = 0;                  // text position when that '*' was seen

    while (*s) {
        int adv = 0;                         // consumed *s against the current token?
        if (*p == '*') {
            star_p = ++p;                    // record the one backtrack point...
            star_s = s;                      // ...and try '*' == empty first
            continue;
        } else if (*p == '?') {
            p++; adv = 1;
        } else if (*p == '[') {
            int r = match_class(&p, *s);     // advances p past ']' on a 0/1 result
            if (r < 0) {                     // malformed '[' -> match it literally
                if (*p == *s) { p++; adv = 1; }
            } else if (r) {
                adv = 1;                     // matched; p already advanced
            }                                // r == 0: no match, p past ']', adv stays 0
        } else if (*p == '\\' && p[1]) {     // escaped literal
            if (p[1] == *s) { p += 2; adv = 1; }
        } else {                             // ordinary literal (also the p-at-NUL case)
            if (*p == *s) { p++; adv = 1; }
        }

        if (adv) {
            s++;
        } else if (star_p) {                 // no match here: let the last '*' eat one more
            p = star_p;
            s = ++star_s;
        } else {
            return 0;                        // mismatch with no '*' to fall back on
        }
    }
    while (*p == '*') p++;                    // trailing '*'s match the empty tail
    return *p == '\0';
}

// ---- known-answer self-test (`glob`) ----
int glob_selftest(void) {
    static const struct { const char* pat; const char* str; int want; } c[] = {
        // literals
        { "abc",        "abc",        1 },
        { "abc",        "abd",        0 },
        { "abc",        "ab",         0 },
        { "ab",         "abc",        0 },
        // '?'  (exactly one character)
        { "a?c",        "abc",        1 },
        { "a?c",        "ac",         0 },
        { "?",          "",           0 },
        { "?",          "x",          1 },
        // '*'  (any run, including empty)
        { "*",          "",           1 },
        { "*",          "anything",   1 },
        { "*.c",        "main.c",     1 },
        { "*.c",        "main.h",     0 },
        { "a*b",        "axxxb",      1 },
        { "a*b",        "axxxc",      0 },
        { "a*b*c",      "a-b-c",      1 },
        { "a*b*c",      "abXc",       1 },
        // character classes / ranges / negation
        { "[abc]",      "b",          1 },
        { "[abc]",      "d",          0 },
        { "[a-z]",      "m",          1 },
        { "[a-z]",      "M",          0 },
        { "[0-9]*",     "42files",    1 },
        { "[!0-9]",     "a",          1 },
        { "[!0-9]",     "5",          0 },
        { "[^a-z]",     "A",          1 },
        { "[]x]",       "]",          1 },   // ']' first slot is a literal member
        { "file[0-9].txt", "file7.txt", 1 },
        { "file[0-9].txt", "fileX.txt", 0 },
        // escaping a metacharacter
        { "a\\*b",      "a*b",        1 },
        { "a\\*b",      "axb",        0 },
        { "\\?",        "?",          1 },
        // unterminated class -> literal '['
        { "[abc",       "[abc",       1 },
        // empty-string edges
        { "",           "",           1 },
        { "",           "x",          0 },
        // catastrophic-backtracking pattern against a long non-matching text: the
        // naive recursive matcher blows up exponentially here — this engine must
        // return 0 quickly, so the test completing at all is the proof.
        { "*a*a*a*a*a*a*a*a*b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 0 },
    };
    int total = (int)(sizeof(c) / sizeof(c[0]));
    int pass = 0;
    for (int i = 0; i < total; i++) {
        int got = glob_match(c[i].pat, c[i].str);
        if (got == c[i].want) pass++;
        else printf("glob: FAIL pat=\"%s\" str=\"%s\" want=%d got=%d\n",
                    c[i].pat, c[i].str, c[i].want, got);
    }
    printf("glob: self-test %d/%d passed\n", pass, total);
    return (pass == total) ? 0 : -1;
}
