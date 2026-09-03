#include "libc.h"

/* caesar — the classic Caesar / ROT-N letter-shift cipher. Each ASCII letter is rotated
 * forward by N positions within its own case (wrapping z->a, Z->A); digits, punctuation and
 * whitespace pass through untouched. N defaults to 13 (the self-inverse ROT13), may be
 * negative, and is taken mod 26, so `caesar 13` twice — or `caesar -13` once — decodes.
 *
 *   caesar 3 "Attack at dawn"   -> Dwwdfn dw gdzq
 *   caesar "hello"              -> uryyb            (no N -> ROT13)
 *   echo Uryyb | caesar         -> Hello
 *
 * The text comes from the ARGS (joined with spaces) or, when only N (or nothing) is given,
 * from stdin. Pure argv/stdin -> stdout; built INSIDE the OS by `cc` — installs via
 * `xbm install caesar`.
 */

/* Is s a signed decimal integer (a lone sign is not)? Fills *out when so. */
static int is_int(const char* s, int* out) {
    const char* p = s;
    int neg = 0;
    if (*p == '-' || *p == '+') { neg = (*p == '-'); p++; }
    if (!*p) return 0;
    int v = 0;
    for (; *p; p++) { if (*p < '0' || *p > '9') return 0; v = v * 10 + (*p - '0'); }
    *out = neg ? -v : v;
    return 1;
}

static char shift_char(char c, int n) {
    /* ((x + n) mod 26), corrected to stay non-negative for a negative shift */
    if (c >= 'a' && c <= 'z') return (char)('a' + (((c - 'a' + n) % 26) + 26) % 26);
    if (c >= 'A' && c <= 'Z') return (char)('A' + (((c - 'A' + n) % 26) + 26) % 26);
    return c;
}

static void emit_shifted(const char* s, int n) {
    for (int i = 0; s[i]; i++) putchar(shift_char(s[i], n));
}

int main(int argc, char** argv) {
    int n = 13;              /* default ROT13 */
    int first_text = 1;      /* index of the first text arg */
    if (argc >= 2 && is_int(argv[1], &n)) first_text = 2;

    if (first_text < argc) {
        for (int a = first_text; a < argc; a++) {
            if (a > first_text) putchar(' ');
            emit_shifted(argv[a], n);
        }
        putchar('\n');
    } else {
        /* no text args: filter stdin, shifting letters line by line */
        char line[512];
        while (fgets(line, sizeof(line), stdin)) emit_shifted(line, n);
    }
    return 0;
}
