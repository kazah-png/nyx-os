#include "kernel.h"
#include "apic.h"
#include "smp.h"

cpu_info_t cpu_info[MAX_CPUS];
uint32_t cpu_count = 1;

extern uint8_t _binary_trampoline_bin_start[];
extern uint8_t _binary_trampoline_bin_end[];

static int detect_aps_via_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    if (eax < 1) return 0;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int logical_count = (ebx >> 16) & 0xFF;
    int bsp_apic_id = (ebx >> 24) & 0xFF;

    if (logical_count <= 1) return 0;

    cpu_info[0].apic_id = (uint32_t)bsp_apic_id;
    cpu_info[0].cpu_number = 0;
    cpu_info[0].started = 1;
    cpu_info[0].running = 1;

    int ap_found = 0;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0xB), "c"(0));
    if (eax && ecx) {
        int apic_ids[MAX_CPUS], apic_count = 0;
        for (int sub = 0; sub < 2; sub++) {
            __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0xB), "c"(sub));
            if (!eax && !ebx) break;
            if ((ecx & 0xFF) == 1) {
                int total = (ebx & 0xFFFF);
                if (total > MAX_CPUS) total = MAX_CPUS;
                for (int id = 0; id < total; id++) {
                    int dup = 0;
                    for (int j = 0; j < apic_count; j++)
                        if (apic_ids[j] == id) { dup = 1; break; }
                    if (!dup) apic_ids[apic_count++] = id;
                }
            }
        }
        if (apic_count > 1) {
            for (int idx = 1; idx < apic_count && idx < MAX_CPUS; idx++) {
                cpu_info[idx].apic_id = (uint32_t)apic_ids[idx];
                cpu_info[idx].cpu_number = idx;
                cpu_info[idx].started = 0;
                cpu_info[idx].running = 0;
            }
            return apic_count;
        }
    }

    ap_found = logical_count;
    if (ap_found > MAX_CPUS) ap_found = MAX_CPUS;
    for (int idx = 1; idx < ap_found; idx++) {
        cpu_info[idx].apic_id = (uint32_t)(bsp_apic_id + idx);
        cpu_info[idx].cpu_number = idx;
        cpu_info[idx].started = 0;
        cpu_info[idx].running = 0;
    }
    return ap_found > 1 ? ap_found : 0;
}

void smp_init(void) {
    for (int i = 0; i < MAX_CPUS; i++) {
        cpu_info[i].apic_id = 0xFF;
        cpu_info[i].cpu_number = i;
        cpu_info[i].started = 0;
        cpu_info[i].running = 0;
    }

    int ap_count = detect_aps_via_cpuid();
    if (ap_count <= 1) {
        printf("[SMP] Single CPU.\n");
        return;
    }

    printf("[SMP] %d AP(s) found via CPUID\n", ap_count - 1);

    uint32_t tramp_size = (uint32_t)(uint64_t)_binary_trampoline_bin_end -
                          (uint32_t)(uint64_t)_binary_trampoline_bin_start;
    if (tramp_size > 0x1000) tramp_size = 0x1000;
    __builtin_memcpy((void*)0x8000, _binary_trampoline_bin_start, tramp_size);

    for (int i = 1; i < ap_count && i < MAX_CPUS; i++) {
        uint32_t apic_id = cpu_info[i].apic_id;
        if (apic_id == 0xFF) continue;

        void* stack = kmalloc(AP_STACK_SIZE);
        if (!stack) continue;
        cpu_info[i].stack_base = (uint64_t)stack;
        cpu_info[i].stack_top = (uint64_t)stack + AP_STACK_SIZE;

        extern uint64_t kernel_pml4_phys;
        uint32_t data_off = tramp_size - 32;
        volatile uint64_t* td_data = (volatile uint64_t*)(0x8000 + data_off);
        td_data[0] = kernel_pml4_phys;
        td_data[1] = cpu_info[i].stack_top;
        volatile uint32_t* cpu_id_ptr = (volatile uint32_t*)((uint8_t*)td_data + 16);
        cpu_id_ptr[0] = (uint32_t)i;
        volatile uint64_t* entry_ptr = (volatile uint64_t*)((uint8_t*)td_data + 24);
        entry_ptr[0] = (uint64_t)ap_main;

        if (!lapic) { printf("[SMP] No LAPIC, skipping AP %d\n", i); continue; }
        volatile uint32_t* apic = lapic;

        // INIT assert — 10 ms delay via io_wait (outb to port 0x80 triggers
        // TCG VCPU exit, allowing QEMU to process IPI delivery).
        apic[LAPIC_ICR1/4] = apic_id << 24;
        apic[LAPIC_ICR0/4] = 0x000C500;
        for (volatile int d = 0; d < 500; d++) io_wait();

        // INIT de-assert
        apic[LAPIC_ICR1/4] = apic_id << 24;
        apic[LAPIC_ICR0/4] = 0x0008500;
        for (volatile int d = 0; d < 500; d++) io_wait();

        // SIPI — send once, poll for AP start with io_wait. Each outb gives
        // the AP VCPU execution time in single-threaded TCG.
        apic[LAPIC_ICR1/4] = apic_id << 24;
        apic[LAPIC_ICR0/4] = 0x608;
        for (int retry = 0; retry < 200 && !cpu_info[i].started; retry++) {
            for (volatile int d = 0; d < 50; d++) io_wait();
        }

        if (cpu_info[i].started) {
            cpu_count++;
            printf("[SMP] AP %d (APIC %d) started\n", i, apic_id);
        } else {
            printf("[SMP] AP %d (APIC %d) FAILED to start\n", i, apic_id);
        }
    }

    printf("[SMP] %d CPU(s) online\n", cpu_count);
}

void ap_main(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return;

    cpu_info[cpu_id].started = 1;
    cpu_info[cpu_id].running = 1;

    // HLT yields VCPU to BSP in single-threaded TCG. cli prevents NMIs
    // (which lack handler setup) from waking the AP.
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}
