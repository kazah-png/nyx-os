#include "libc.h"

/* nbase — show a non-negative integer in decimal, hex, octal and binary.
 *
 *   nbase NUM [NUM...]
 *
 * Each NUM's base is auto-detected from its prefix: 0x/0X hex, 0b/0B binary,
 * 0o/0O octal, a leading 0 with more digits classic-octal, otherwise decimal.
 * For every value nbase prints one line:  <dec>  0x<hex>  0o<oct>  0b<bin>
 * so the daily "what's 0x2A in binary?" needs no calculator. Non-negative 64-bit
 * integers only; an unparsable NUM reports an error and sets the exit status to
 * 1, but the remaining arguments are still converted. Pure argv + stdout, built
 * INSIDE the OS by the in-OS `cc` — installs via `xbm install nbase`.
 */

static int digit_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse s as a non-negative integer into *out. Returns 0 on success, -1 on an
 * empty/prefix-only token or a digit out of range for the detected base. */
static int parse_num(const char* s, unsigned long long* out) {
    int base = 10;
    const char* p = s;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
    else if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) { base = 2;  p += 2; }
    else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) { base = 8;  p += 2; }
    else if (p[0] == '0' && p[1] != '\0') { base = 8; p += 1; }   /* classic 0NNN octal */
    if (*p == '\0') return -1;                                    /* just a prefix, no digits */
    unsigned long long v = 0;
    for (; *p; p++) {
        int d = digit_val(*p);
        if (d < 0 || d >= base) return -1;
        v = v * (unsigned long long)base + (unsigned long long)d;
    }
    *out = v;
    return 0;
}

/* Emit v in base (2..16) with no leading zeros (0 -> "0"). */
static void emit_base(unsigned long long v, int base) {
    char buf[65];
    const char* digits = "0123456789abcdef";
    int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v) { buf[n++] = digits[(int)(v % (unsigned long long)base)]; v /= (unsigned long long)base; }
    while (n) putchar(buf[--n]);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: nbase NUM [NUM...]\n");
        printf("  NUM base auto-detected: 0x hex, 0b binary, 0o/0NNN octal, else decimal\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        unsigned long long v;
        if (parse_num(argv[i], &v) != 0) {
            printf("nbase: invalid number: %s\n", argv[i]);
            rc = 1;
            continue;
        }
        emit_base(v, 10);
        printf("  0x"); emit_base(v, 16);
        printf("  0o"); emit_base(v, 8);
        printf("  0b"); emit_base(v, 2);
        putchar('\n');
    }
    return rc;
}
