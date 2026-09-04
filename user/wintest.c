#include "libc.h"
#include "uwin.h"

/* wintest — the first ring-3 program to open a REAL desktop window through the
 * v6.4.354 window syscalls (#77). It creates a 320x200 window, fills a Nyx-purple
 * gradient with a moving white square, presents it every frame, and polls for
 * click / key / close events — reporting each over serial. It exits after ~5 s or
 * when the window is closed. This is the end-to-end proof that win_create /
 * win_present / win_poll_event work from user space, composited by the desktop. */

#define W 320
#define H 200

int main(void) {
    int id = win_create(W, H, "wintest");
    printf("wintest: win_create -> id %d\n", id);
    if (id < 0) { printf("wintest: win_create FAILED\n"); return 1; }

    unsigned int* buf = (unsigned int*)malloc(W * H * 4);
    if (!buf) { printf("wintest: malloc FAILED\n"); win_destroy(id); return 1; }

    int frames = 0, events = 0;
    for (int i = 0; i < 160; i++) {                 /* ~160 * 30 ms ~= 4.8 s */
        int sqx = (i * 3) % (W - 40);               /* the square marches right */
        for (int y = 0; y < H; y++) {               /* purple: R + B ramp, low G */
            unsigned int r = 40 + (unsigned int)(y * 130 / H);
            unsigned int b = 70 + (unsigned int)(y * 150 / H);
            uwin_hline(buf, W, H, 0, y, W, (r << 16) | (0x18u << 8) | b);
        }
        uwin_fill_rect(buf, W, H, sqx, H / 2 - 20, 40, 40, 0x00FFFFFF);   /* white marker */
        if (win_present(id, buf, W, H) != 0) { printf("wintest: present FAILED at frame %d\n", i); break; }
        if (i == 0) printf("wintest: first present OK\n");
        frames++;

        win_event_t ev;
        int r;
        while ((r = win_poll_event(id, &ev)) == 1) {
            events++;
            if      (ev.kind == UWE_CLICK) printf("wintest: CLICK x=%ld y=%ld btn=%ld\n", ev.a, ev.b, ev.c);
            else if (ev.kind == UWE_KEY)   printf("wintest: KEY code=%ld\n", ev.a);
            else if (ev.kind == UWE_CLOSE) { printf("wintest: CLOSE requested\n"); goto done; }
        }
        if (r < 0) { printf("wintest: window gone\n"); goto done; }
        usleep(30000);
    }
done:
    printf("wintest: %d frames, %d events; destroying window\n", frames, events);
    win_destroy(id);
    free(buf);
    printf("wintest: done\n");
    return 0;
}
