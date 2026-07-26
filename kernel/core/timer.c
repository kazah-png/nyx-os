// ============================================================
// timer.c - Temporizador del sistema NyxOS (PIT a 1000 Hz, IRQ)
// ============================================================
#include "kernel.h"

static uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 1000;
static uint32_t pit_divisor = 0;

void init_timer(uint32_t frequency) {
    timer_frequency = frequency;
    timer_ticks = 0;
    pit_divisor = 1193180 / frequency;

    // Configurar PIT en modo 2 (rate generator, IRQ on terminal count)
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(pit_divisor & 0xFF));
    outb(0x40, (uint8_t)((pit_divisor >> 8) & 0xFF));

    printf("[TIMER] %d Hz (interrupt-driven)\n", frequency);
}

uint32_t get_ticks(void) {
    return tick_count;
}

void sleep(uint32_t milliseconds) {
    uint32_t ticks_to_wait = (milliseconds * timer_frequency) / 1000;
    uint32_t target = tick_count + ticks_to_wait;

    // With the preemptive scheduler running, block on the timer wait queue instead
    // of busy-waiting: mark ourselves PROC_BLOCKED with a wake_tick and yield, so
    // the scheduler runs other threads and wakes us (irq_scheduler_tick) once
    // tick_count reaches the target. Only the compositor/kernel-thread contexts
    // call sleep(), all with interrupts enabled — never an IRQ handler.
    process_t* self = sched_is_enabled() ? get_current_process() : NULL;
    if (self) {
        for (;;) {
            // cli makes the deadline check + block atomic vs. the waking tick.
            __asm__ volatile("cli");
            if ((int32_t)(tick_count - target) >= 0) { __asm__ volatile("sti"); break; }
            self->wake_tick = target;
            self->state = PROC_BLOCKED;
            __asm__ volatile("sti; hlt");   // parked until the scheduler wakes us
        }
        self->wake_tick = 0;
    } else {
        // No scheduler yet (early boot): nothing else to run, just idle to the tick.
        while ((int32_t)(tick_count - target) < 0) __asm__ volatile("hlt");
    }
}