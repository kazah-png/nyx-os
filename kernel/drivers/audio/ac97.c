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

/* NABM PCM-OUT box (16 bytes at nabm_base + 0x10). */
#define PO_BDBAR  0x10   /* u32: physical base of the buffer-descriptor list  */
#define PO_CIV    0x14   /* u8:  current index value (ro)                     */
#define PO_LVI    0x15   /* u8:  last valid index                             */
#define PO_SR     0x16   /* u16: status (bit0 DCH halted, bit3 LVBCI, bit2 CELV) */
#define PO_PICB   0x18   /* u16: position in current buffer, samples left (ro)*/
#define PO_CR     0x1B   /* u8:  control (bit0 run, bit1 reset, bit4 IOCE)    */

/* A buffer-descriptor: a 32-bit sample-buffer physical address, a 16-bit length in
 * SAMPLES (16-bit units), and control bits (bit15 IOC = interrupt on completion,
 * bit14 BUP = buffer-underrun-policy). Matches the AC'97 hardware layout exactly. */
typedef struct { uint32_t addr; uint16_t len; uint16_t ctrl; } __attribute__((packed)) ac97_bd_t;

#define AC97_TONE_FRAMES 11025               /* ~0.25 s at 44.1 kHz               */
#define AC97_TONE_HZ     440
#define AC97_BD_MAX_SAMP 0xFFFE              /* per-descriptor length limit (samples) */
/* Static, identity-mapped DMA memory: the kernel maps low RAM 1:1, so a .bss
 * buffer's virtual address IS its physical address (the same trick sb16.c uses).
 * The BDL is 8-byte aligned as the engine requires. `ac97_pcm_buf` holds interleaved
 * 16-bit stereo frames (L,R) for either the built-in tone or a decoded WAV. */
static ac97_bd_t ac97_bdl[32] __attribute__((aligned(8)));
static int16_t   ac97_pcm_buf[AC97_MAX_FRAMES * 2];

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

/* Play the first `frames` interleaved-stereo frames already sitting in ac97_pcm_buf:
 * build the BDL (split across entries at the per-descriptor sample limit), arm + run
 * the PCM-out engine, then poll the position bounded. Fills *out with what happened. */
static int ac97_run_frames(uint32_t frames, ac97_play_t* out) {
    if (out) { out->started = 0; out->picb_start = 0; out->picb_end = 0;
               out->civ_end = 0; out->sr = 0; out->moved = 0; }
    ac97_dev_t* d = &g_ac97;
    if (frames == 0) return 0;
    /* DMA addresses must fit the 32-bit descriptor fields (kernel lives in low RAM). */
    if (((uintptr_t)ac97_pcm_buf >> 32) || ((uintptr_t)ac97_bdl >> 32)) return 0;

    /* Chunk the buffer into descriptors of at most AC97_BD_MAX_SAMP samples each. */
    uint32_t per_frames = AC97_BD_MAX_SAMP / 2;          /* stereo: 2 samples/frame */
    int n = 0;
    for (uint32_t f = 0; f < frames && n < 32; f += per_frames, n++) {
        uint32_t chunk = frames - f;
        if (chunk > per_frames) chunk = per_frames;
        ac97_bdl[n].addr = (uint32_t)(uintptr_t)(ac97_pcm_buf + (size_t)f * 2);
        ac97_bdl[n].len  = (uint16_t)(chunk * 2);        /* length in 16-bit samples */
        ac97_bdl[n].ctrl = 0x8000;                       /* IOC */
    }
    if (n == 0) return 0;

    uint16_t po = d->nabm_base;
    outb(po + PO_CR, 0x02);                              /* reset engine */
    for (int i = 0; i < 100000 && (inb(po + PO_CR) & 0x02); i++) io_wait();
    outl(po + PO_BDBAR, (uint32_t)(uintptr_t)ac97_bdl);
    outb(po + PO_LVI, (uint8_t)(n - 1));                 /* last valid index */
    outb(po + PO_CR, 0x01);                              /* RPBM run */

    uint16_t picb_start = 0;
    for (int i = 0; i < 20000 && picb_start == 0; i++) { picb_start = inw(po + PO_PICB); io_wait(); }

    uint16_t picb_min = picb_start ? picb_start : 0xFFFF;
    int halted = 0;
    for (int i = 0; i < 8000000; i++) {
        uint16_t p = inw(po + PO_PICB);
        if (p < picb_min) picb_min = p;
        if (inw(po + PO_SR) & 0x01) { halted = 1; break; }   /* DCH */
        io_wait();
    }
    uint16_t sr  = inw(po + PO_SR);
    uint8_t  civ = inb(po + PO_CIV);
    outb(po + PO_CR, 0x00);                              /* stop; leave codec up */

    int moved = (picb_min < picb_start) || halted;
    if (out) {
        out->started    = 1;
        out->picb_start = picb_start;
        out->picb_end   = picb_min;
        out->civ_end    = civ;
        out->sr         = sr;
        out->moved      = moved;
    }
    return moved;
}

int ac97_play_tone(ac97_play_t* out) {
    if (!ac97_init()) { if (out) out->moved = 0; return 0; }    /* need a ready codec (R2) */
    int half = (44100 / AC97_TONE_HZ) / 2;
    if (half < 1) half = 1;
    for (int f = 0; f < AC97_TONE_FRAMES; f++) {
        int16_t s = ((f / half) & 1) ? (int16_t)-8000 : (int16_t)8000;
        ac97_pcm_buf[f * 2]     = s;
        ac97_pcm_buf[f * 2 + 1] = s;
    }
    return ac97_run_frames(AC97_TONE_FRAMES, out);
}

int ac97_play_pcm(const int16_t* samples, uint32_t frames, uint32_t rate) {
    if (!samples || frames == 0) return 0;
    if (!ac97_init()) return 0;
    if (frames > AC97_MAX_FRAMES) frames = AC97_MAX_FRAMES;     /* bounded copy */

    /* Select the DAC sample rate (VRA was enabled in bring-up), then stage the PCM. */
    if (rate >= 8000 && rate <= 48000) outw(g_ac97.nam_base + NAM_PCM_RATE, (uint16_t)rate);
    for (uint32_t i = 0; i < frames * 2; i++) ac97_pcm_buf[i] = samples[i];
    return ac97_run_frames(frames, 0);
}

const ac97_dev_t* ac97_get(void) { return &g_ac97; }
