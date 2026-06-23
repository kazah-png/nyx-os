#ifndef SB16_H
#define SB16_H

#include "kernel.h"

#define SB16_BASE_PORT      0x220
#define SB16_MIXER_ADDR     (SB16_BASE_PORT + 0x04)
#define SB16_MIXER_DATA     (SB16_BASE_PORT + 0x05)
#define SB16_DSP_RESET      (SB16_BASE_PORT + 0x06)
#define SB16_DSP_READ       (SB16_BASE_PORT + 0x0A)
#define SB16_DSP_WRITE      (SB16_BASE_PORT + 0x0C)
#define SB16_DSP_WRITE_STAT (SB16_BASE_PORT + 0x0C)
#define SB16_DSP_READ_STAT  (SB16_BASE_PORT + 0x0E)

#define SB16_DSP_RESET_VAL  0x01
#define SB16_DSP_ACK        0xAA

#define SB16_CMD_ENABLE_SPEAKER  0xD1
#define SB16_CMD_DISABLE_SPEAKER 0xD3
#define SB16_CMD_SET_TIME_CONST  0x40
#define SB16_CMD_SET_OUTPUT_RATE 0x41
#define SB16_CMD_SET_INPUT_RATE  0x42
#define SB16_CMD_8BIT_AUTO_INIT  0xB0
#define SB16_CMD_16BIT_AUTO_INIT 0xB6
#define SB16_CMD_EXIT_8BIT_AUTO  0xD9
#define SB16_CMD_EXIT_16BIT_AUTO 0xD8

#define SB16_MIXER_MASTER_VOL    0x22
#define SB16_MIXER_PCM_VOL       0x04
#define SB16_MIXER_SYNTH_VOL     0x26
#define SB16_MIXER_CD_VOL        0x28
#define SB16_MIXER_LINE_VOL      0x2E
#define SB16_MIXER_MIC_VOL       0x0A

#define SB16_DMA_CHANNEL_16BIT   5
#define SB16_DMA_CHANNEL_8BIT    1
#define SB16_IRQ                 5

#define SB16_SAMPLE_RATE         22050

typedef struct {
    uint16_t base_port;
    uint8_t  irq;
    uint8_t  dma8;
    uint8_t  dma16;
    uint8_t  initialized;
    uint32_t sample_rate;
    uint16_t dma_buffer_size;
    uint8_t* dma_buffer;
    uint32_t dma_buffer_phys;
} sb16_t;

int sb16_init(void);
int sb16_reset_dsp(void);
int sb16_write_dsp(uint8_t cmd);
uint8_t sb16_read_dsp(void);
int sb16_set_sample_rate(uint32_t rate);
int sb16_set_mixer(uint8_t reg, uint8_t value);
uint8_t sb16_get_mixer(uint8_t reg);
int sb16_start_dma(uint8_t* buffer, uint32_t len, uint8_t bits);
void sb16_stop_dma(void);
void sb16_irq_handler(void);
void sb16_play_sound(const uint8_t* data, uint32_t len, uint32_t freq, uint8_t bits);

#endif