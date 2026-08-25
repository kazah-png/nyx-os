#ifndef AC97_H
#define AC97_H

#include "../../core/kernel.h"

/*
 * Intel AC'97 audio controller (QEMU `-device AC97` = the 82801AA / ICH codec bus).
 *
 * The controller straddles two I/O windows named by their PCI BARs:
 *   - NAMBAR  (BAR0) — Native Audio Mixer: the codec/mixer registers (volumes,
 *     sample rate, the codec vendor id).
 *   - NABMBAR (BAR1) — Native Audio Bus Master: the DMA engine (buffer-descriptor
 *     list pointer, run/status, the PCM-out "box").
 *
 * Rung 1 (v6.4.359): DETECTION — find the controller by PCI class 04:01, capture
 * the two BAR bases + IRQ line. Writes nothing to the device.
 * Rung 2 (v6.4.360): CODEC BRING-UP — enable PCI I/O + bus-mastering, take the
 * codec out of cold reset, wait for primary-codec-ready, read the codec vendor id
 * + capabilities, and unmute + set master/PCM volume + a 44.1 kHz sample rate.
 * BDL/DMA playback (rung 3) still to come; bring-up only runs when ac97_init is
 * called, so the boot and SB16 are untouched.
 */
typedef struct {
    int      found;
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint16_t nam_base;    /* BAR0, I/O base — mixer / codec registers  */
    uint16_t nabm_base;   /* BAR1, I/O base — bus-master DMA registers  */
    uint8_t  irq;         /* PCI interrupt line                         */
    /* --- rung 2: codec bring-up state --- */
    int      inited;      /* ac97_init has run                          */
    int      ready;       /* primary-codec-ready asserted               */
    uint32_t codec_id;    /* (vendor1 << 16) | vendor2                  */
    uint16_t caps;        /* NAM 0x00 reset/capabilities readback       */
    uint16_t rate;        /* NAM 0x2C PCM front-DAC rate readback (Hz)  */
    uint16_t master_vol;  /* NAM 0x02 master-volume readback            */
} ac97_dev_t;

/* Result of a bus-master playback run — what the PCM-out DMA engine did, so the
 * `ac97` command can prove (headless, where audio is inaudible) that DMA advanced. */
typedef struct {
    int      started;      /* the engine was armed + run                       */
    uint16_t picb_start;   /* position-in-current-buffer right after start      */
    uint16_t picb_end;     /* ... after the bounded poll (should have dropped)  */
    uint8_t  civ_end;      /* current buffer index reached                      */
    uint16_t sr;           /* PCM-out status register at the end (bit0 = halted)*/
    int      moved;        /* PICB decremented or the engine halted = DMA ran   */
} ac97_play_t;

/* Probe PCI once (result cached). Returns 1 if an AC'97 controller is present. */
int ac97_detect(void);
/* Bring the codec up (idempotent): enable the controller, reset + ready-wait,
 * read the vendor id, unmute + set volumes/rate. Returns 1 if the codec is ready. */
int ac97_init(void);
/* Play a built-in ~0.25 s 440 Hz square tone through the PCM-out bus-master DMA
 * engine (builds a 1-entry BDL over a static stereo buffer, arms + runs it, then
 * polls the position registers). Fills *out with what the engine did. Returns 1
 * if DMA was observed to advance. */
int ac97_play_tone(ac97_play_t* out);
/* The detected device (found == 0 until a successful ac97_detect). */
const ac97_dev_t* ac97_get(void);

#endif
