#include "libc.h"

/* morse — International Morse code encoder / decoder.
 *
 *   morse TEXT...        encode text to Morse
 *   morse -d CODE...     decode Morse back to text (uppercase)
 *
 * Encoding: each letter/digit becomes its dot-dash code; codes within a word are
 * separated by a single space and words by " / ". Case-insensitive; unknown
 * characters are skipped. Decoding is the inverse — Morse tokens are read from the
 * arguments (joined with spaces, so `morse -d ... --- ...` works without quoting),
 * a "/" token becomes a space, and each code maps back to its letter. Pure argv +
 * stdout, deterministic, built INSIDE the OS by `cc` — installs via `xbm install morse`.
 */

static const char* const MC[36] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",     /* A-J */
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",       /* K-T */
    "..-", "...-", ".--", "-..-", "-.--", "--..",                              /* U-Z */
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."  /* 0-9 */
};

/* char -> code index (A-Z 0..25, 0-9 26..35), or -1 if unsupported. */
static int idx_of(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    return -1;
}
static char char_of(int i) {
    if (i >= 0 && i < 26) return (char)('A' + i);
    if (i >= 26 && i < 36) return (char)('0' + (i - 26));
    return 0;
}
static void put_str(const char* s) { while (*s) putchar(*s++); }

/* Join argv[from..argc) with single spaces into buf (bounded). */
static void join_args(int argc, char** argv, int from, char* buf, int cap) {
    int n = 0;
    for (int i = from; i < argc; i++) {
        if (i > from && n < cap - 1) buf[n++] = ' ';
        for (const char* p = argv[i]; *p && n < cap - 1; p++) buf[n++] = *p;
    }
    buf[n] = 0;
}

int main(int argc, char** argv) {
    int decode = 0, from = 1;
    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'd' && argv[1][2] == 0) { decode = 1; from = 2; }
    if (from >= argc) {
        put_str("usage: morse [-d] TEXT...\n");
        put_str("  encode text to Morse, or -d to decode (space-separated codes, / = word gap)\n");
        return 1;
    }
    char buf[512];
    join_args(argc, argv, from, buf, sizeof buf);

    if (!decode) {
        int started = 0, wbreak = 0;
        for (const char* p = buf; *p; p++) {
            if (*p == ' ') { if (started) wbreak = 1; continue; }   /* word boundary */
            int ix = idx_of(*p);
            if (ix < 0) continue;                                   /* skip unsupported chars */
            if (started) put_str(wbreak ? " / " : " ");
            put_str(MC[ix]);
            started = 1; wbreak = 0;
        }
        putchar('\n');
    } else {
        char tok[16]; int tn = 0;
        for (const char* p = buf; ; p++) {
            if (*p == ' ' || *p == 0) {                             /* end of a token */
                if (tn > 0) {
                    tok[tn] = 0;
                    if (tok[0] == '/' && tok[1] == 0) {
                        putchar(' ');                               /* word gap */
                    } else {
                        for (int i = 0; i < 36; i++)
                            if (strcmp(tok, MC[i]) == 0) { putchar(char_of(i)); break; }
                    }
                    tn = 0;
                }
                if (*p == 0) break;
            } else if (tn < 15) {
                tok[tn++] = *p;
            }
        }
        putchar('\n');
    }
    return 0;
}
