#include "ac97.h"
#include "../misc/pci.h"

/* PCI config-space offsets (mechanism #1). */
#define PCI_BAR0    0x10
#define PCI_BAR1    0x14
#define PCI_INTLINE 0x3C

static ac97_dev_t g_ac97;

/* Mask an I/O BAR down to its port base: bit 0 is the I/O-space indicator and
 * bit 1 is reserved, so the base lives in bits 15..2 (I/O ports are 16-bit). */
static uint16_t io_bar_base(uint32_t bar) {
    return (uint16_t)(bar & 0xFFFCu);
}

int ac97_detect(void) {
    if (g_ac97.found) return 1;                 /* cached */

    pci_dev_t devs[32];
    int n = pci_enumerate(devs, 32);
    for (int i = 0; i < n; i++) {
        /* class 0x04 = multimedia controller, subclass 0x01 = audio (AC'97). */
        if (devs[i].class_code != 0x04 || devs[i].subclass != 0x01) continue;

        uint8_t b = devs[i].bus, s = devs[i].slot, f = devs[i].func;
        uint32_t bar0 = pci_cfg_read32(b, s, f, PCI_BAR0);
        uint32_t bar1 = pci_cfg_read32(b, s, f, PCI_BAR1);
        uint32_t intr = pci_cfg_read32(b, s, f, PCI_INTLINE);

        g_ac97.found     = 1;
        g_ac97.bus = b; g_ac97.slot = s; g_ac97.func = f;
        g_ac97.vendor    = devs[i].vendor;
        g_ac97.device    = devs[i].device;
        g_ac97.nam_base  = io_bar_base(bar0);
        g_ac97.nabm_base = io_bar_base(bar1);
        g_ac97.irq       = (uint8_t)(intr & 0xFF);
        return 1;
    }
    g_ac97.found = 0;
    return 0;
}

const ac97_dev_t* ac97_get(void) { return &g_ac97; }
