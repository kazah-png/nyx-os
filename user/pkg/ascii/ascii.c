#include "libc.h"

/* ascii — a small developer reference: look up a character's code, or print the table.
 *
 *   ascii            print the whole table (codes 0-127: decimal, hex, octal, glyph)
 *   ascii <char>     show the code of a single character   (e.g. `ascii A`)
 *   ascii <code>     show the character for a code          (dec 65, hex 0x41, octal 0101)
 *
 * Control codes render in caret notation (^@ .. ^_, ^? for DEL); space shows as SPC.
 * Pure argv -> stdout; installs via `xbm install ascii`.
 */

static void show(int c) {
    char r[4];
    if      (c == 32)  { r[0] = 'S'; r[1] = 'P'; r[2] = 'C'; r[3] = 0; }   /* space */
    else if (c == 127) { r[0] = '^'; r[1] = '?'; r[2] = 0; }               /* DEL */
    else if (c < 32)   { r[0] = '^'; r[1] = (char)('@' + c); r[2] = 0; }   /* other controls */
    else               { r[0] = (char)c; r[1] = 0; }
    printf("%3d  0x%02X  0%03o  %s\n", c, c, c, r);
}

int main(int argc, char** argv) {
    if (argc < 2) {                                   /* no args: the full table */
        for (int c = 0; c < 128; c++) show(c);
        return 0;
    }
    const char* a = argv[1];
    int c;
    if (a[0] >= '0' && a[0] <= '9') c = (int)strtol(a, (char**)0, 0);   /* code: dec / 0xHH / 0ooo */
    else if (a[1] == '\0')          c = (unsigned char)a[0];            /* a single character */
    else { printf("Usage: ascii            (print the ASCII table)\n"
                  "       ascii <char>|<code>\n"); return 1; }
    if (c < 0 || c > 127) { printf("ascii: value out of range (0-127)\n"); return 1; }
    show(c);
    return 0;
}
