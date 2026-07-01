#ifndef SMP_H
#define SMP_H

#define MAX_CPUS 8
#define AP_STACK_SIZE 8192

typedef struct {
    uint32_t apic_id;
    uint32_t cpu_number;
    uint64_t stack_base;
    uint64_t stack_top;
    volatile int started;
    volatile int running;
} cpu_info_t;

extern cpu_info_t cpu_info[MAX_CPUS];
extern uint32_t cpu_count;

void smp_init(void);
void ap_main(uint32_t cpu_id);

#endif
