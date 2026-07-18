#ifndef SMP_H
#define SMP_H

#define MAX_CPUS 8
#define AP_STACK_SIZE 8192

// Interrupt vectors owned by the application processors. The LAPIC timer is a
// per-CPU device, so this vector only ever fires on the core it was programmed
// on — the BSP keeps its own timer masked and stays on the PIT's IRQ0.
#define AP_TIMER_VECTOR     0x40
#define AP_SPURIOUS_VECTOR  0xFF

// The per-CPU block. Anything a core may touch while another core runs belongs
// here, indexed by CPU, never in a kernel global. Today that is just the tick
// counter; the scheduler cursor and user_cr3/user_rsp move here when APs start
// running user processes.
typedef struct {
    uint32_t apic_id;       // APIC id the BSP assigned/expects for this CPU
    uint32_t apic_id_self;  // APIC id the CPU read from its OWN Local APIC (proof)
    uint32_t cpu_number;
    uint64_t stack_base;
    uint64_t stack_top;
    volatile int started;
    volatile int running;
    volatile uint64_t ticks; // THIS core's LAPIC-timer ticks (APs only; BSP uses tick_count)
} cpu_info_t;

extern cpu_info_t cpu_info[MAX_CPUS];
extern uint32_t cpu_count;

void smp_init(void);
void ap_main(uint32_t cpu_id);
cpu_info_t* cpu_self(void);
void ap_timer_tick(void);

#endif
