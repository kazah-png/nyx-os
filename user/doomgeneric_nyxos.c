// ============================================================
// doomgeneric_nyxos.c - NyxOS USERSPACE platform layer for doomgeneric (v5.9.32)
// ============================================================
// doomgeneric asks each platform to implement six hooks (see doom_src/doomgeneric.h).
// This wires them to the userspace syscalls added for the DOOM milestone:
//   DG_DrawFrame   -> SYS_FBPRESENT   (v5.9.29) : blit the 32bpp frame, scaled
//   DG_GetKey      -> SYS_GETKEYEVENT (v5.9.30) : raw key make/break -> doomkey
//   DG_GetTicksMs  -> SYS_GETTIMEOFDAY          : ms since first call
//   DG_SleepMs     -> SYS_NANOSLEEP
// It talks to the kernel directly (a tiny inline `syscall`) rather than including
// user/syscall.h, so DOOM's own freestanding libc shims (fake_stdlib.h etc.) are the
// only headers in play and there are no type clashes. Build with CMAP256 UNDEFINED so
// the engine's pixel_t is 32bpp 0x00RRGGBB — byte-identical to what fbpresent expects.
#include "doomgeneric.h"
#include "doomkeys.h"

#define SYS_NANOSLEEP    53
#define SYS_GETTIMEOFDAY 52
#define SYS_FBPRESENT    55
#define SYS_GETKEYEVENT  56

static long dg_syscall3(long n, long a, long b, long c) {
    long ret;
    register long r10 asm("r10") = 0;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

// ---- the frame buffer the engine renders into ----
static pixel_t nyx_screen[DOOMGENERIC_RESX * DOOMGENERIC_RESY];
pixel_t* DG_ScreenBuffer = nyx_screen;

void DG_Init(void) { /* the compositor yields to us on the first present */ }

void DG_DrawFrame(void) {
    dg_syscall3(SYS_FBPRESENT, (long)DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_SleepMs(uint32_t ms) {
    long ts[2] = { (long)(ms / 1000), (long)(ms % 1000) * 1000000L };  // {sec, nsec}
    dg_syscall3(SYS_NANOSLEEP, (long)ts, 0, 0);
}

uint32_t DG_GetTicksMs(void) {
    long tv[2] = { 0, 0 };                          // {sec, usec}
    dg_syscall3(SYS_GETTIMEOFDAY, (long)tv, 0, 0);
    static uint64_t base = 0;
    uint64_t now = (uint64_t)tv[0] * 1000u + (uint64_t)tv[1] / 1000u;
    if (base == 0) base = now;
    return (uint32_t)(now - base);
}

// Map a NyxOS raw scancode (Set-1; bit 0x80 = E0-extended) to a doomkey.
static unsigned char scancode_to_doomkey(int code) {
    switch (code) {
        case 0xC8: return KEY_UPARROW;      // E0 48
        case 0xD0: return KEY_DOWNARROW;    // E0 50
        case 0xCB: return KEY_LEFTARROW;    // E0 4B
        case 0xCD: return KEY_RIGHTARROW;   // E0 4D
        case 0x1D: return KEY_FIRE;         // left ctrl
        case 0x39: return KEY_USE;          // space
        case 0x38: return KEY_LALT;         // left alt (strafe modifier)
        case 0x2A: return KEY_RSHIFT;       // left shift (run)
        case 0x1C: return KEY_ENTER;        // enter
        case 0x01: return KEY_ESCAPE;       // esc
        case 0x0F: return KEY_TAB;          // tab (automap)
        case 0x0E: return KEY_BACKSPACE;
        default: break;
    }
    // Letters and digits -> their lowercase ASCII (menus, cheats). US positional map.
    static const char row1[] = "1234567890-=";     // scancodes 0x02..0x0D
    static const char rowq[] = "qwertyuiop[]";      // 0x10..0x1B
    static const char rowa[] = "asdfghjkl;'`";      // 0x1E..0x29
    static const char rowz[] = "\\zxcvbnm,./";      // 0x2B..0x35
    if (code >= 0x02 && code <= 0x0D) return (unsigned char)row1[code - 0x02];
    if (code >= 0x10 && code <= 0x1B) return (unsigned char)rowq[code - 0x10];
    if (code >= 0x1E && code <= 0x29) return (unsigned char)rowa[code - 0x1E];
    if (code >= 0x2B && code <= 0x35) return (unsigned char)rowz[code - 0x2B];
    return 0;
}

int DG_GetKey(int* pressed, unsigned char* doomKey) {
    long ev = dg_syscall3(SYS_GETKEYEVENT, 0, 0, 0);   // (pressed<<8)|scancode, or -1
    if (ev < 0) return 0;
    unsigned char dk = scancode_to_doomkey((int)(ev & 0xFF));
    if (dk == 0) return 0;                              // key we don't map: skip
    *pressed = (int)((ev >> 8) & 1);
    *doomKey = dk;
    return 1;
}

void DG_SetWindowTitle(const char* title) { (void)title; }

int main(int argc, char** argv) {
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
    return 0;
}
