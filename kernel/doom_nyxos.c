// ============================================================
// doom_nyxos.c - Capa de adaptacion de DOOM para NyxOS
// ============================================================
#define CMAP256 1
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200
#include "doom_src/doom.h"
#include "doom_src/doomgeneric.h"
#include "doom_src/doomkeys.h"
#include "doom_src/m_fixed.h"
#include "doom_src/m_argv.h"
#include "kernel.h"

uint8_t screenBuffer[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
pixel_t* DG_ScreenBuffer = (pixel_t*)screenBuffer;
int doom_quit_requested = 0;

void DG_Init(void) {
    vga_set_mode_13h();
}

static int dump_frames = 100;
extern byte* I_VideoBuffer;
void DG_DrawFrame(void) {
    // Copy DOOM's I_VideoBuffer directly (bypass I_FinishUpdate offsets)
    if (I_VideoBuffer) {
        memcpy_asm(screenBuffer, I_VideoBuffer, 320 * 200);
    } else {
        serial_puts("[DG] I_VideoBuffer is NULL!\n");
    }
    vga_copy_buffer(screenBuffer);
    if (dump_frames > 0) {
        dump_frames--;
        serial_puts("[FB] ");
        for (int i = 0; i < 48; i++) {
            char hex[4];
            hex[0] = "0123456789ABCDEF"[(screenBuffer[i] >> 4) & 0xF];
            hex[1] = "0123456789ABCDEF"[screenBuffer[i] & 0xF];
            hex[2] = ' ';
            hex[3] = '\0';
            serial_puts(hex);
        }
        serial_puts("\n");
    }
}

int DG_GetKey(int* pressed, unsigned char* key) {
    char c = getchar_poll();
    if (!c) return 0;
    *pressed = 1;
    switch (c) {
        case 'w': *key = KEY_UPARROW; break;
        case 's': *key = KEY_DOWNARROW; break;
        case 'a': *key = KEY_LEFTARROW; break;
        case 'd': *key = KEY_RIGHTARROW; break;
        case ' ': *key = KEY_FIRE; break;
        case 0x0D: *key = KEY_ENTER; break;
        case 0x1B: *key = KEY_ESCAPE; break;
        default: *key = (unsigned char)c; break;
    }
    return 1;
}

void DG_SleepMs(uint32_t ms) {
    uint32_t start = tick_count;
    while ((tick_count - start) < ms) {
        __asm__ volatile("hlt");
    }
}

uint32_t DG_GetTicksMs(void) {
    return tick_count;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;
}

void run_doom(void) {
    static char* doom_argv[] = { "doom", NULL };
    myargc = 1;
    myargv = doom_argv;
    serial_puts("[DOOM] Starting...\n");
    M_FindResponseFile();
    serial_puts("[DOOM] Response file found, calling DG_Init...\n");
    DG_Init();
    serial_puts("[DOOM] Mode 13h set, drawing test pattern...\n");
    // Draw a test pattern: checkerboard + color bars
    for (int y = 0; y < 200; y++) {
        for (int x = 0; x < 320; x++) {
            if (y < 20) screenBuffer[y * 320 + x] = 15;       // white bar top
            else if (y >= 180) screenBuffer[y * 320 + x] = 15; // white bar bottom
            else if (x < 80) screenBuffer[y * 320 + x] = 36;   // bar 1
            else if (x < 160) screenBuffer[y * 320 + x] = 44; // bar 2
            else if (x < 240) screenBuffer[y * 320 + x] = 52; // bar 3
            else screenBuffer[y * 320 + x] = 16;              // bar 4
        }
    }
    // Draw text "DOOM!" at top
    const char* msg = "DOOM TEST PATTERN";
    for (int i = 0; msg[i]; i++) {
        for (int py = 0; py < 8; py++) {
            for (int px = 0; px < 8; px++) {
                int dx = (i * 8 + px) + 80;
                int dy = py + 30;
                if (dx < 320 && dy < 200)
                    screenBuffer[dy * 320 + dx] = 0; // black bg
            }
        }
    }
    // Set VGA palette: RAINBOW so DOOM pixels look very colorful
    for (int i = 0; i < 256; i++) {
        outb(0x3C8, i);
        if (i == 0) { outb(0x3C9, 0); outb(0x3C9, 0); outb(0x3C9, 0); }          // black
        else if (i == 15) { outb(0x3C9,63); outb(0x3C9,63); outb(0x3C9,63); }    // white
        else if ((i % 8) == 0) { outb(0x3C9,63); outb(0x3C9, 0); outb(0x3C9, 0);}  // red
        else if ((i % 8) == 1) { outb(0x3C9, 0); outb(0x3C9,63); outb(0x3C9, 0);}  // green
        else if ((i % 8) == 2) { outb(0x3C9, 0); outb(0x3C9, 0); outb(0x3C9,63);}  // blue
        else if ((i % 8) == 3) { outb(0x3C9,63); outb(0x3C9,63); outb(0x3C9, 0);}  // yellow
        else if ((i % 8) == 4) { outb(0x3C9, 0); outb(0x3C9,63); outb(0x3C9,63);}  // cyan
        else if ((i % 8) == 5) { outb(0x3C9,63); outb(0x3C9, 0); outb(0x3C9,63);}  // magenta
        else if ((i % 8) == 6) { outb(0x3C9,63); outb(0x3C9,32); outb(0x3C9, 0);}  // orange
        else { outb(0x3C9,32); outb(0x3C9,32); outb(0x3C9,32); }                  // gray
    }

    DG_DrawFrame();
    serial_puts("[DOOM] Test pattern displayed, waiting 3s...\n");
    DG_SleepMs(3000);
    serial_puts("[DOOM] Calling D_DoomMain...\n");
    D_DoomMain();
    serial_puts("[DOOM] D_DoomMain returned!\n");
    init_screen();
    clear_screen();
    serial_puts("[DOOM] Back to text mode.\n");
}

void cmd_doomtest(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("DOOM test: checking WAD...\n");
    FILE* f = fopen("doom1.wad", "rb");
    if (!f) { printf("FAIL: fopen returned NULL\n"); return; }
    uint8_t hdr[12];
    int n = fread(hdr, 1, 12, f);
    printf("fread=%d, pos=%d, ", n, ftell(f));
    printf("magic=%.4s, lumps=%d, infotableofs=%d\n",
        hdr, *(int*)(hdr+4), *(int*)(hdr+8));
    fseek(f, 0, SEEK_END);
    printf("WAD size: %d bytes\n", ftell(f));
    fclose(f);
    printf("DOOM test PASSED\n");
}
