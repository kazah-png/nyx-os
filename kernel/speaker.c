#include "kernel.h"
#include "speaker.h"

#define PIT_FREQ 1193180

void speaker_init(void) {
    // Ensure speaker is off
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

void speaker_on(uint32_t frequency) {
    if (frequency == 0) { speaker_off(); return; }
    uint32_t divisor = PIT_FREQ / frequency;
    // Program PIT channel 2 (port 0x42) in mode 3 (square wave)
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
    // Enable speaker (bits 0 and 1)
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);
}

void speaker_off(void) {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

void speaker_beep(uint32_t frequency, uint32_t duration_ms) {
    speaker_on(frequency);
    sleep(duration_ms);
    speaker_off();
}

void speaker_play_note(uint32_t frequency, uint32_t duration_ms) {
    if (frequency == 0) {
        sleep(duration_ms);
        return;
    }
    speaker_beep(frequency, duration_ms);
    // Small gap between notes
    sleep(10);
}
