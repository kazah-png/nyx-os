#include "libc.h"

/* fbtest — exercise the v5.9.29 fullscreen-framebuffer syscalls (the DOOM graphics
 * enabler). Queries the screen geometry, renders three vertical colour bars into a
 * small 320x200 buffer, and presents it every frame for a few seconds. The kernel
 * scales the 320x200 source to the full screen (nearest-neighbour) and, while we keep
 * presenting, the desktop compositor yields the screen to us. The three saturated
 * primary bars make the result unambiguous in a screendump: left third RED, middle
 * GREEN, right third BLUE, filling the whole screen. */

#define RW 320
#define RH 200

int main(void) {
    unsigned int info[3] = { 0, 0, 0 };
    if (fbinfo(info) != 0) { printf("fbtest: fbinfo FAILED\n"); return 1; }
    printf("fbtest: screen %ux%u, %u bpp\n", info[0], info[1], info[2]);
    if (info[0] == 0 || info[2] != 32) { printf("fbtest: unexpected screen format\n"); return 1; }

    unsigned int* buf = (unsigned int*)malloc(RW * RH * 4);
    if (!buf) { printf("fbtest: malloc FAILED\n"); return 1; }

    for (int y = 0; y < RH; y++) {
        for (int x = 0; x < RW; x++) {
            unsigned int c;
            if (x < RW / 3)          c = 0x00FF0000;   /* red   */
            else if (x < 2 * RW / 3) c = 0x0000FF00;   /* green */
            else                     c = 0x000000FF;   /* blue  */
            buf[y * RW + x] = c;
        }
    }

    printf("fbtest: presenting RGB bars (scaled to fullscreen) for ~3s\n");
    for (int i = 0; i < 100; i++) {
        if (fbpresent(buf, RW, RH) != 0) { printf("fbtest: fbpresent FAILED at frame %d\n", i); free(buf); return 1; }
        usleep(30000);   /* ~30 ms/frame -> keeps fullscreen ownership alive */
    }

    free(buf);
    printf("fbtest: done (100 frames presented OK)\n");
    return 0;
}
