#include "kernel.h"
#include "sb16.h"

#define PIT_FREQ 1193180
#define ISA_DMA_MAX 0x1000000

// DMA controller ports (8237)
#define DMA1_MASK      0x0A
#define DMA1_MODE      0x0B
#define DMA1_CLEAR_FF  0x0C
#define DMA1_CHAN_ADDR 0x02
#define DMA1_CHAN_COUNT 0x03

#define DMA2_MASK      0xD4
#define DMA2_MODE      0xD6
#define DMA2_CLEAR_FF  0xD8
#define DMA2_CHAN5_ADDR_L 0xC4
#define DMA2_CHAN5_ADDR_H 0xC5
#define DMA2_CHAN5_COUNT_L 0xC6
#define DMA2_CHAN5_COUNT_H 0xC7

static sb16_t sb16;
static volatile int sb16_irq_fired = 0;

static void sb16_short_delay(void) {
    for (volatile int i = 0; i < 10000; i++) __asm__ __volatile__("");
}

int sb16_reset_dsp(void) {
    outb(SB16_DSP_RESET, 1);
    sb16_short_delay();
    outb(SB16_DSP_RESET, 0);
    sb16_short_delay();
    uint8_t ack = inb(SB16_DSP_READ);
    if (ack != 0xAA) {
        serial_puts("[SB16] DSP reset failed\n");
        return -1;
    }
    serial_puts("[SB16] DSP reset OK\n");
    return 0;
}

int sb16_write_dsp(uint8_t cmd) {
    int timeout = 10000;
    while (timeout--) {
        if (!(inb(SB16_DSP_WRITE_STAT) & 0x80)) {
            outb(SB16_DSP_WRITE, cmd);
            return 0;
            }
    }
    return -1;
}

uint8_t sb16_read_dsp(void) {
    int timeout = 10000;
    while (timeout--) {
        if (inb(SB16_DSP_READ_STAT) & 0x80) {
            return inb(SB16_DSP_READ);
            }
    }
    return 0;
}

uint16_t sb16_get_dsp_version(void) {
    if (sb16_write_dsp(0xE1) < 0) return 0;
    uint8_t major = sb16_read_dsp();
    uint8_t minor = sb16_read_dsp();
    return ((uint16_t)major << 8) | minor;
}

int sb16_set_sample_rate(uint32_t rate) {
    if (sb16.base_port == 0) return -1;
    if (sb16_write_dsp(SB16_CMD_SET_OUTPUT_RATE) < 0) return -1;
    if (sb16_write_dsp((uint8_t)(rate >> 8)) < 0) return -1;
    if (sb16_write_dsp((uint8_t)(rate & 0xFF)) < 0) return -1;
    sb16.sample_rate = rate;
    return 0;
}

int sb16_set_mixer(uint8_t reg, uint8_t value) {
    outb(SB16_MIXER_ADDR, reg);
    outb(SB16_MIXER_DATA, value);
    return 0;
}

uint8_t sb16_get_mixer(uint8_t reg) {
    outb(SB16_MIXER_ADDR, reg);
    return inb(SB16_MIXER_DATA);
}

static void dma_clear_ff(int is16bit) {
    if (is16bit) outb(DMA2_CLEAR_FF, 0);
    else outb(DMA1_CLEAR_FF, 0);
}

static void dma_set_mask(int channel, int mask) {
    if (channel >= 4) {
        outb(DMA2_MASK, mask ? (channel & 3) | 4 : (channel & 3));
    } else {
        outb(DMA1_MASK, mask ? (1 << channel) : (channel & 3));
    }
}

static void dma_set_mode(int channel, uint8_t mode) {
    if (channel >= 4) {
        outb(DMA2_MODE, mode | (channel & 3));
    } else {
        outb(DMA1_MODE, mode | channel);
    }
}

int sb16_start_dma(uint8_t* buffer, uint32_t len, uint8_t bits) {
    if (!buffer || len == 0) return -1;
    uint32_t phys = (uint32_t)buffer;
    if (phys >= ISA_DMA_MAX) return -1;

    uint8_t channel = (bits == 16) ? SB16_DMA_CHANNEL_16BIT : SB16_DMA_CHANNEL_8BIT;
    uint8_t is16bit = (bits == 16);
    uint16_t dma_len = len - 1;

    dma_set_mask(channel, 1);
    dma_clear_ff(is16bit);

    if (is16bit) {
        uint32_t addr = phys >> 1;
        uint8_t page = (addr >> 16) & 0xFF;
        outb(0x8B, page);
        outb(DMA2_CHAN5_ADDR_L, addr & 0xFF);
        outb(DMA2_CHAN5_ADDR_H, (addr >> 8) & 0xFF);
        dma_clear_ff(is16bit);
        outb(DMA2_CHAN5_COUNT_L, dma_len & 0xFF);
        outb(DMA2_CHAN5_COUNT_H, (dma_len >> 8) & 0xFF);
        dma_set_mode(channel, 0x50);
    } else {
        uint8_t page = (phys >> 16) & 0xFF;
        outb(0x83, page);
        outb(DMA1_CHAN_ADDR, phys & 0xFF);
        outb(DMA1_CHAN_ADDR, (phys >> 8) & 0xFF);
        dma_clear_ff(is16bit);
        outb(DMA1_CHAN_COUNT, dma_len & 0xFF);
        outb(DMA1_CHAN_COUNT, (dma_len >> 8) & 0xFF);
        dma_set_mode(channel, 0x58);
    }

    dma_set_mask(channel, 0);
    return 0;
}

void sb16_stop_dma(void) {
    uint8_t cmd = (sb16.sample_rate > 44100) ? SB16_CMD_EXIT_16BIT_AUTO : SB16_CMD_EXIT_8BIT_AUTO;
    sb16_write_dsp(cmd);
}

int sb16_start_playback(uint32_t len, uint8_t bits) {
    if (bits == 16) {
        uint8_t mode = 0x10;
        if (sb16.sample_rate > 22050) mode = 0x30;
        else if (sb16.sample_rate > 11025) mode = 0x20;
        if (sb16_write_dsp(SB16_CMD_16BIT_AUTO_INIT) < 0) return -1;
        if (sb16_write_dsp(mode) < 0) return -1;
        len >>= 1;
        if (sb16_write_dsp(len & 0xFF) < 0) return -1;
        if (sb16_write_dsp((len >> 8) & 0xFF) < 0) return -1;
    } else {
        uint8_t mode = 0x00;
        if (sb16.sample_rate <= 11025) mode = 0x2C;
        else if (sb16.sample_rate <= 22050) mode = 0x1C;
        else mode = 0x00;
        if (sb16_write_dsp(SB16_CMD_8BIT_AUTO_INIT) < 0) return -1;
        if (sb16_write_dsp(mode) < 0) return -1;
        if (sb16_write_dsp(len & 0xFF) < 0) return -1;
        if (sb16_write_dsp((len >> 8) & 0xFF) < 0) return -1;
    }
    return 0;
}

void sb16_irq_handler(void) {
    sb16_irq_fired = 1;
    uint8_t status = inb(SB16_DSP_READ_STAT);
    (void)status;
}

void sb16_play_sound(const uint8_t* data, uint32_t len, uint32_t freq, uint8_t bits) {
    if (!sb16.initialized || !sb16.dma_buffer) return;
    if (len > sb16.dma_buffer_size) len = sb16.dma_buffer_size;
    sb16_set_sample_rate(freq);
    memcpy(sb16.dma_buffer, data, len);
    sb16_start_dma(sb16.dma_buffer, len, bits);
    sb16_start_playback(len, bits);
}

int sb16_init(void) {
    sb16.base_port = SB16_BASE_PORT;
    sb16.irq = SB16_IRQ;
    sb16.dma8 = SB16_DMA_CHANNEL_8BIT;
    sb16.dma16 = SB16_DMA_CHANNEL_16BIT;
    sb16.initialized = 0;
    sb16.sample_rate = SB16_SAMPLE_RATE;
    sb16.dma_buffer_size = 65536;
    sb16.dma_buffer = NULL;
    sb16.dma_buffer_phys = 0;

    if (sb16_reset_dsp() < 0) return -1;

    uint16_t version = sb16_get_dsp_version();
    serial_puts("[SB16] DSP version ");
    { char hex[4]; hex[0] = "0123456789ABCDEF"[(version >> 4) & 0xF];
        hex[1] = "0123456789ABCDEF"[version & 0xF];
        hex[2] = '.'; hex[3] = '\0'; serial_puts(hex); }
    serial_puts("\n");
    if (version < 0x0400) {
        serial_puts("[SB16] Warning: DSP version < 4.0, some features may not work\n");
    }

    sb16_set_sample_rate(SB16_SAMPLE_RATE);
    sb16_set_mixer(SB16_MIXER_MASTER_VOL, 0x30);
    sb16_set_mixer(SB16_MIXER_PCM_VOL, 0x30);

    void* buf = kmalloc_aligned(sb16.dma_buffer_size, 4096);
    if (!buf) {
        serial_puts("[SB16] Failed to allocate DMA buffer\n");
        return -1;
    }
    uint32_t phys = (uint32_t)buf;
    if (phys >= ISA_DMA_MAX) {
        serial_puts("[SB16] DMA buffer above 16MB, trying page alloc\n");
        kfree(buf);
        buf = alloc_page();
        if (!buf || (uint32_t)buf >= ISA_DMA_MAX) {
            serial_puts("[SB16] Failed to get low DMA buffer\n");
            return -1;
        }
        phys = (uint32_t)buf;
    }
    sb16.dma_buffer = (uint8_t*)buf;
    sb16.dma_buffer_phys = phys;
    memset_asm(sb16.dma_buffer, 0, sb16.dma_buffer_size);
    sb16.initialized = 1;
    serial_puts("[SB16] Initialized successfully\n");
    return 0;
}
