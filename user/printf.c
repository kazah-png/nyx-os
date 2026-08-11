#include "libc.h"

/* printf — format and print ARGUMENTS under control of FORMAT, like printf(1).
 *
 * Conversions:  %d %i (signed), %u %o %x %X (unsigned), %c, %s, %%  — each with
 *               '-' (left), '0' (zero-pad), '+'/' ' (sign) flags, a field width,
 *               and a '.'precision (min digits for integers, max chars for %s).
 * Escapes in FORMAT:  \n \t \r \\ \a \b \f \v \"  plus \NNN (1-3 octal digits),
 *               \xHH (1-2 hex digits), and \c (stop all further output).
 * Argument recycling:  when more arguments remain than the FORMAT consumes, the
 *               FORMAT is reused until they run out (a FORMAT with no conversions
 *               is printed exactly once). Missing arguments count as 0 / "".
 *
 * Floating conversions (%f %e %g) and %b are not implemented. */

static void emit(const char* s, int n) { if (n > 0) write(1, s, n); }
static void emitc(char c)               { write(1, &c, 1); }
static void emit_rep(char c, int n)     { while (n-- > 0) write(1, &c, 1); }

/* Digits of v in `base` into buf (most-significant first); returns the length. */
static int utoa(unsigned long v, int base, int upper, char* buf) {
    char tmp[32]; int n = 0;
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = d[v % (unsigned)base]; v /= (unsigned)base; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

/* Emit one integer conversion with all of width/precision/flags applied. */
static void put_int(unsigned long mag, int negative, int is_signed, int base,
                    int upper, int width, int prec, int left, int zero,
                    int plus, int space) {
    char digits[32];
    int dl = utoa(mag, base, upper, digits);
    /* NB: GNU coreutils printf keeps the "0" for %.0d of 0 (it does NOT apply C's
     * precision-0-of-value-0 suppression), so we don't special-case it here. */

    char sign = 0;
    if (is_signed) {
        if (negative)   sign = '-';
        else if (plus)  sign = '+';
        else if (space) sign = ' ';
    }

    int zeros_prec = (prec > dl) ? prec - dl : 0;  /* precision zero-fill */
    int body = (sign ? 1 : 0) + zeros_prec + dl;
    int pad  = (width > body) ? width - body : 0;
    /* the '0' flag is ignored when left-justifying or when a precision is given */
    int zero_pad  = (zero && !left && prec < 0) ? pad : 0;
    int space_pad = pad - zero_pad;

    if (!left) emit_rep(' ', space_pad);
    if (sign)  emitc(sign);
    emit_rep('0', zero_pad);
    emit_rep('0', zeros_prec);
    emit(digits, dl);
    if (left)  emit_rep(' ', space_pad);
}

static int is_odigit(char c) { return c >= '0' && c <= '7'; }
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        const char* e = "printf: missing operand\n";
        write(2, e, (int)strlen(e));
        return 1;
    }
    const char* fmt = argv[1];
    int ai = 2;                  /* next argument index into argv */
    int stop = 0;               /* set by \c: suppress all further output */
    int used_arg = 0;           /* did the current FORMAT pass consume a real argument? */

    do {
        used_arg = 0;
        const char* p = fmt;
        while (*p && !stop) {
            if (*p == '\\') {
                p++;
                if (is_odigit(*p)) {                 /* \NNN octal, up to 3 digits */
                    int v = 0, k = 0;
                    while (k < 3 && is_odigit(*p)) { v = v * 8 + (*p - '0'); p++; k++; }
                    emitc((char)v);
                } else if (*p == 'x' && hexval(p[1]) >= 0) {  /* \xHH hex, up to 2 */
                    p++; int v = 0, k = 0;
                    while (k < 2 && hexval(*p) >= 0) { v = v * 16 + hexval(*p); p++; k++; }
                    emitc((char)v);
                } else {
                    char c = *p;
                    switch (c) {
                        case 'n': emitc('\n'); break;  case 't': emitc('\t'); break;
                        case 'r': emitc('\r'); break;  case '\\': emitc('\\'); break;
                        case 'a': emitc('\a'); break;  case 'b': emitc('\b'); break;
                        case 'f': emitc('\f'); break;  case 'v': emitc('\v'); break;
                        case '"': emitc('"');  break;  case 'c': stop = 1;    break;
                        case '\0': emitc('\\'); continue;   /* trailing backslash: literal */
                        default:  emitc('\\'); emitc(c); break;
                    }
                    if (c) p++;
                }
                continue;
            }
            if (*p != '%') { emitc(*p); p++; continue; }

            p++;                                     /* past '%' */
            if (*p == '%') { emitc('%'); p++; continue; }

            int left = 0, zero = 0, plus = 0, space = 0;
            for (;; p++) {
                if      (*p == '-') left  = 1;
                else if (*p == '0') zero  = 1;
                else if (*p == '+') plus  = 1;
                else if (*p == ' ') space = 1;
                else break;
            }
            int width = 0;
            while (isdigit((unsigned char)*p)) { width = width * 10 + (*p - '0'); p++; }
            int prec = -1;
            if (*p == '.') { p++; prec = 0; while (isdigit((unsigned char)*p)) { prec = prec * 10 + (*p - '0'); p++; } }
            char conv = *p; if (conv) p++;

            /* fetch the next argument (only counts as "used" if one is really there) */
            const char* a = "";
            if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
                conv == 'x' || conv == 'X' || conv == 'c' || conv == 's') {
                if (ai < argc) { a = argv[ai++]; used_arg = 1; }
            }

            switch (conv) {
                case 'd': case 'i': {
                    long v = strtol(a, 0, 0);
                    unsigned long mag = (v < 0) ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
                    put_int(mag, v < 0, 1, 10, 0, width, prec, left, zero, plus, space);
                    break;
                }
                case 'u': put_int((unsigned long)strtol(a, 0, 0), 0, 0, 10, 0, width, prec, left, zero, 0, 0); break;
                case 'o': put_int((unsigned long)strtol(a, 0, 0), 0, 0,  8, 0, width, prec, left, zero, 0, 0); break;
                case 'x': put_int((unsigned long)strtol(a, 0, 0), 0, 0, 16, 0, width, prec, left, zero, 0, 0); break;
                case 'X': put_int((unsigned long)strtol(a, 0, 0), 0, 0, 16, 1, width, prec, left, zero, 0, 0); break;
                case 'c': {
                    int len = a[0] ? 1 : 0;
                    int pad = (width > len) ? width - len : 0;
                    if (!left) emit_rep(' ', pad);
                    if (len)   emitc(a[0]);
                    if (left)  emit_rep(' ', pad);
                    break;
                }
                case 's': {
                    int len = (int)strlen(a);
                    if (prec >= 0 && prec < len) len = prec;
                    int pad = (width > len) ? width - len : 0;
                    if (!left) emit_rep(' ', pad);
                    emit(a, len);
                    if (left)  emit_rep(' ', pad);
                    break;
                }
                default:  /* unknown/absent conversion: emit it literally */
                    emitc('%');
                    if (conv) emitc(conv);
                    break;
            }
        }
    } while (ai < argc && used_arg && !stop);  /* recycle while args remain AND a pass
                                                * consumed one (else a conversion-less
                                                * FORMAT with extra args would loop) */

    return 0;
}
