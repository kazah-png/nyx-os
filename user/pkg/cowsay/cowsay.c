#include "libc.h"

/* cowsay — the classic ASCII cow that says whatever you tell it.
 *
 * The iconic "first thing you install" package, now for NyxOS's xbm package
 * manager:  `xbm install cowsay`  then  `cowsay Hello from NyxOS`. Pure string
 * handling + stdout, compiled INSIDE the OS by the in-OS `cc` — a small, cheerful
 * proof that xbm turns source in user/pkg into a first-class command.
 *
 * The message goes in a speech bubble whose top and bottom borders are two
 * characters wider than the text; below it stands the cow.
 *
 * Usage:  cowsay [words...]        (a default moo if you give none)
 */

int main(int argc, char** argv) {
    char msg[256];
    int len = 0;

    /* Join the arguments with single spaces into the bubble text. */
    for (int i = 1; i < argc; i++) {
        if (i > 1 && len < (int)sizeof(msg) - 1) msg[len++] = ' ';
        for (const char* p = argv[i]; *p && len < (int)sizeof(msg) - 1; p++)
            msg[len++] = *p;
    }
    if (len == 0) {                                   /* no words -> a default moo */
        const char* d = "Moo from NyxOS";
        while (d[len] && len < (int)sizeof(msg) - 1) { msg[len] = d[len]; len++; }
    }
    msg[len] = '\0';

    /* Top border: a leading space, then '_' two wider than the text. */
    putchar(' ');
    for (int i = 0; i < len + 2; i++) putchar('_');
    putchar('\n');

    printf("< %s >\n", msg);                          /* the spoken line */

    /* Bottom border. */
    putchar(' ');
    for (int i = 0; i < len + 2; i++) putchar('-');
    putchar('\n');

    /* The cow. */
    printf("        \\   ^__^\n");
    printf("         \\  (oo)\\_______\n");
    printf("            (__)\\       )\\/\\\n");
    printf("                ||----w |\n");
    printf("                ||     ||\n");
    return 0;
}
