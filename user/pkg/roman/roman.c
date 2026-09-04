#include "libc.h"

/* roman — convert between arabic integers and Roman numerals, both ways. A token that
 * is all digits is read as a number (1..3999) and printed as a Roman numeral; a token
 * of Roman letters (IVXLCDM, any case) is read as a numeral and printed as its arabic
 * value. Values outside 1..3999 have no standard Roman form and are rejected, as is any
 * non-canonical numeral (e.g. IIII, VV, IC) — a numeral is accepted only if it round-trips
 * to the one canonical spelling of its value.
 *
 *   roman 1994          -> MCMXCIV
 *   roman MCMXCIV       -> 1994
 *   roman 4 9 40 2024   -> IV / IX / XL / MMXXIV
 *   echo xiv | roman    -> 14
 *
 * Tokens come from the ARGS, or — when none are given — from stdin (whitespace-separated).
 * Pure argv/stdin -> stdout; built INSIDE the OS by `cc` — installs via `xbm install roman`.
 */

static const int  RVAL[13] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
static const char* RSYM[13] = {"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"};

/* Encode 1..3999 into out (max "MMMDCCCLXXXVIII" = 15 chars + NUL). */
static void to_roman(int n, char* out) {
    out[0] = '\0';
    for (int i = 0; i < 13; i++)
        while (n >= RVAL[i]) { strcat(out, RSYM[i]); n -= RVAL[i]; }
}

static int roman_digit(char c) {
    switch (c >= 'a' && c <= 'z' ? c - 32 : c) {
        case 'I': return 1;   case 'V': return 5;   case 'X': return 10;
        case 'L': return 50;  case 'C': return 100;  case 'D': return 500;
        case 'M': return 1000; default: return 0;
    }
}

/* Parse a Roman numeral; returns 1 and fills *out only for a valid CANONICAL numeral. */
static int from_roman(const char* s, int* out) {
    if (!s[0]) return 0;
    int total = 0, prev = 0;
    for (const char* p = s; *p; p++) {
        int v = roman_digit(*p);
        if (v == 0) return 0;                    /* not a Roman letter */
        total += (v > prev) ? (v - 2 * prev) : v;
        prev = v;
    }
    if (total < 1 || total > 3999) return 0;
    char canon[24];
    to_roman(total, canon);                      /* accept only the canonical spelling */
    for (int i = 0; s[i] || canon[i]; i++) {
        char u = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
        if (u != canon[i]) return 0;
    }
    *out = total;
    return 1;
}

static void do_token(const char* tok) {
    if (!tok[0]) return;
    int alldigits = 1;
    for (const char* p = tok; *p; p++) if (*p < '0' || *p > '9') { alldigits = 0; break; }
    if (alldigits) {
        long n = strtol(tok, (char**)0, 10);
        if (n < 1 || n > 3999) {
            fprintf(stderr, "roman: %s out of range (1..3999)\n", tok);
            return;
        }
        char out[24];
        to_roman((int)n, out);
        printf("%s\n", out);
    } else {
        int v;
        if (!from_roman(tok, &v)) {
            fprintf(stderr, "roman: '%s' is not a valid Roman numeral\n", tok);
            return;
        }
        printf("%d\n", v);
    }
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        for (int i = 1; i < argc; i++) do_token(argv[i]);
    } else {
        char line[512];
        while (fgets(line, sizeof(line), stdin)) {
            char tok[64];
            int t = 0;
            for (const char* p = line; ; p++) {
                if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\0') {
                    if (t > 0) { tok[t] = '\0'; do_token(tok); t = 0; }
                    if (*p == '\0') break;
                } else if (t < (int)sizeof(tok) - 1) {
                    tok[t++] = *p;
                }
            }
        }
    }
    return 0;
}
