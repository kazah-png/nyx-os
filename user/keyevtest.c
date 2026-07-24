#include "libc.h"

/* keyevtest — exercise the v5.9.30 raw key-event path (DOOM's DG_GetKey enabler).
 * Polls SYS_GETKEYEVENT and prints every make/break. Unlike stdin (cooked, press-
 * only), this reports BOTH press and release, with layout-independent Set-1 scancodes
 * (bit 0x80 = E0-extended arrow/nav). A game reads keys this way. */

int main(void) {
    int p, c;
    /* Drain events buffered while the command line itself was typed, so the report
     * below only shows keys pressed during the polling window. */
    while (getkeyevent(&p, &c)) { }

    printf("keyevtest: polling raw key events for ~4s — press keys now\n");
    int count = 0;
    for (int i = 0; i < 800; i++) {
        while (getkeyevent(&p, &c)) {
            printf("keyevtest: EVENT pressed=%d code=0x%x\n", p, c);
            count++;
        }
        usleep(5000);   /* 5 ms between polls */
    }
    printf("keyevtest: done (%d events)\n", count);
    return 0;
}
