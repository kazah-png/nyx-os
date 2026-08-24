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
 * Rung 1 (v6.4.359) is DETECTION ONLY: find the controller by PCI class 04:01,
 * capture the two BAR bases + IRQ line, and report them. Codec cold-reset + mixer
 * bring-up (rung 2) and BDL/DMA playback (rung 3) come next; this rung writes
 * nothing to the device, so it cannot disturb SB16 or the boot.
 */
typedef struct {
    int      found;
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint16_t nam_base;    /* BAR0, I/O base — mixer / codec registers  */
    uint16_t nabm_base;   /* BAR1, I/O base — bus-master DMA registers  */
    uint8_t  irq;         /* PCI interrupt line                         */
} ac97_dev_t;

/* Probe PCI once (result cached). Returns 1 if an AC'97 controller is present. */
int ac97_detect(void);
/* The detected device (found == 0 until a successful ac97_detect). */
const ac97_dev_t* ac97_get(void);

#endif
