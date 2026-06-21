// ============================================================
// doom_nyxos.c - Capa de adaptacion de DOOM para NyxOS
// ============================================================
#define CMAP256 1
#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 200
#include "kernel.h"
#include "doom_src/doom.h"
#include "doom_src/doomgeneric.h"
#include "doom_src/doomkeys.h"
#include "doom_src/m_fixed.h"
#include "doom_src/m_argv.h"

uint8_t screenBuffer[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
pixel_t* DG_ScreenBuffer = (pixel_t*)screenBuffer;

void DG_Init(void) {
    vga_set_mode_13h();
}

void DG_DrawFrame(void) {
    vga_copy_buffer(screenBuffer);
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
    M_FindResponseFile();
    DG_Init();
    D_DoomMain();
    init_screen();
    clear_screen();
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
