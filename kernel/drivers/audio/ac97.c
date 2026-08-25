#include "ac97.h"
#include "../misc/pci.h"

/* PCI config-space offsets (mechanism #1). */
#define PCI_COMMAND 0x04
#define PCI_BAR0    0x10
#define PCI_BAR1    0x14
#define PCI_INTLINE 0x3C
#define PCI_CMD_IO      0x0001   /* respond to I/O-space accesses */
#define PCI_CMD_MASTER  0x0004   /* enable bus mastering (DMA)    */

/* NAM (Native Audio Mixer, at nam_base) register offsets — the codec side. */
#define NAM_RESET       0x00   /* write = reset codec; read = capabilities */
#define NAM_MASTER_VOL  0x02   /* master out volume (bit15 mute)           */
#define NAM_PCM_VOL     0x18   /* PCM out volume                           */
#define NAM_EXT_CTRL    0x2A   /* extended audio control (bit0 = VRA)      */
#define NAM_PCM_RATE    0x2C   /* PCM front-DAC sample rate (Hz)           */
#define NAM_VENDOR1     0x7C   /* codec vendor id, high word               */
#define NAM_VENDOR2     0x7E   /* codec vendor id, low word                */

/* NABM (Native Audio Bus Master, at nabm_base) global registers — the controller. */
#define NABM_GLOB_CNT   0x2C   /* global control: bit1 = AC'97 cold reset# */
#define NABM_GLOB_STA   0x30   /* global status:  bit8 = primary codec ready */

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

int ac97_init(void) {
    if (!ac97_detect()) return 0;
    ac97_dev_t* d = &g_ac97;
    if (d->inited) return d->ready;            /* bring up once */
    d->inited = 1;

    /* (a) Let the controller respond to I/O + drive DMA (PCI command register). */
    uint32_t cmd = pci_cfg_read32(d->bus, d->slot, d->func, PCI_COMMAND);
    pci_cfg_write32(d->bus, d->slot, d->func, PCI_COMMAND,
                    cmd | PCI_CMD_IO | PCI_CMD_MASTER);

    /* (b) Take the codec out of AC'97 cold reset, then wait (bounded) for the
     *     primary codec to report ready. Never spin forever on bad hardware. */
    outl(d->nabm_base + NABM_GLOB_CNT, 0x00000002);   /* bit1 = cold reset# -> normal */
    d->ready = 0;
    for (int i = 0; i < 100000; i++) {
        if (inl(d->nabm_base + NABM_GLOB_STA) & 0x100) { d->ready = 1; break; }
        io_wait();
    }

    /* (c) Reset the codec registers to defaults, then read capabilities + vendor id. */
    outw(d->nam_base + NAM_RESET, 0);
    d->caps     = inw(d->nam_base + NAM_RESET);
    d->codec_id = ((uint32_t)inw(d->nam_base + NAM_VENDOR1) << 16)
                | (uint32_t)inw(d->nam_base + NAM_VENDOR2);

    /* (d) Unmute + full master & PCM-out volume; enable variable-rate audio and
     *     select 44.1 kHz (what the WAV path will feed in rung 3). */
    outw(d->nam_base + NAM_MASTER_VOL, 0x0000);        /* 0 dB, unmuted */
    outw(d->nam_base + NAM_PCM_VOL,    0x0000);
    outw(d->nam_base + NAM_EXT_CTRL, inw(d->nam_base + NAM_EXT_CTRL) | 0x0001);  /* VRA */
    outw(d->nam_base + NAM_PCM_RATE, 44100);
    d->rate       = inw(d->nam_base + NAM_PCM_RATE);
    d->master_vol = inw(d->nam_base + NAM_MASTER_VOL);
    return d->ready;
}

const ac97_dev_t* ac97_get(void) { return &g_ac97; }
