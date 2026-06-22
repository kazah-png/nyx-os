#include "kernel.h"

#define VBE_INDEX  0x01CE
#define VBE_DATA   0x01CF

#define VBE_DISPI_ID       0
#define VBE_DISPI_XRES     1
#define VBE_DISPI_YRES     2
#define VBE_DISPI_BPP      3
#define VBE_DISPI_ENABLE   4
#define VBE_DISPI_BANK     5
#define VBE_DISPI_VIRT_WIDTH  6

#define VBE_DISPI_DISABLED     0x00
#define VBE_DISPI_ENABLED      0x01
#define VBE_DISPI_LFB_ENABLED  0x40
#define VBE_DISPI_NOCLEARMEM   0x80

#define VBE_DISPI_ID4  0xB0C4

#define LFB_PHYS_BASE 0xFC000000
#define LFB_VIRT_BASE 0xE0000000
#define MAX_FB_PAGES  1024

static int vbe_initialized = 0;
uint32_t vbe_width = 0;
uint32_t vbe_height = 0;
uint32_t vbe_bpp = 0;
void* vbe_lfb = NULL;

static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_INDEX, index);
    outw(VBE_DATA, value);
}

static uint16_t vbe_read(uint16_t index) {
    outw(VBE_INDEX, index);
    return inw(VBE_DATA);
}

int vbe_init(void) {
    vbe_write(VBE_DISPI_ID, VBE_DISPI_ID4);
    uint16_t id = vbe_read(VBE_DISPI_ID);
    if (id != VBE_DISPI_ID4) {
        serial_puts("[VBE] Bochs VBE not detected\n");
        return -1;
    }
    serial_puts("[VBE] Bochs VBE detected\n");
    vbe_initialized = 1;
    return 0;
}

int vbe_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!vbe_initialized && vbe_init() < 0) return -1;

    char buf[16];
    serial_puts("[VBE] Setting mode: "); serial_puts(itoa((int)width, buf, 10));
    serial_puts("x"); serial_puts(itoa((int)height, buf, 10));
    serial_puts("x"); serial_puts(itoa((int)bpp, buf, 10));
    serial_puts("\n");

    vbe_write(VBE_DISPI_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_ID, VBE_DISPI_ID4);
    vbe_write(VBE_DISPI_XRES, width);
    vbe_write(VBE_DISPI_YRES, height);
    vbe_write(VBE_DISPI_BPP, bpp);
    vbe_write(VBE_DISPI_VIRT_WIDTH, width);
    vbe_write(VBE_DISPI_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    vbe_width = width;
    vbe_height = height;
    vbe_bpp = bpp;

    // Map LFB pages
    uint32_t fb_size = width * height * (bpp / 8);
    uint32_t fb_pages = (fb_size + 0xFFF) / 0x1000;
    if (fb_pages > MAX_FB_PAGES) fb_pages = MAX_FB_PAGES;

    if (vbe_lfb) {
        for (uint32_t i = 0; i < MAX_FB_PAGES; i++)
            unmap_page((void*)(LFB_VIRT_BASE + i * 0x1000));
    }

    for (uint32_t i = 0; i < fb_pages; i++) {
        map_page((void*)(LFB_PHYS_BASE + i * 0x1000),
                 (void*)(LFB_VIRT_BASE + i * 0x1000), 3);
    }

    vbe_lfb = (void*)LFB_VIRT_BASE;
    serial_puts("[VBE] LFB mapped at ");
    serial_puts(itoa((int)(uint32_t)vbe_lfb, buf, 16));
    serial_puts(" ("); 
    serial_puts(itoa((int)fb_pages, buf, 10));
    serial_puts(" pages)\n");

    return 0;
}

uint32_t vbe_get_width(void) { return vbe_width; }
uint32_t vbe_get_height(void) { return vbe_height; }
uint32_t vbe_get_bpp(void) { return vbe_bpp; }
void* vbe_get_lfb(void) { return vbe_lfb; }
