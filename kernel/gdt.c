#include "kernel.h"

// AMD64 GDT descriptor format — use raw 64-bit values
// Standard entries: 8 bytes (1 qword)
// TSS descriptor: 16 bytes (2 qwords)
// 5 flat descriptors, then one 16-byte TSS descriptor PER CPU. A TSS may be
// busy on only one core at a time — its descriptor's busy bit is set by `ltr` —
// so cores cannot share one, which is why an AP deliberately skipped `ltr`
// until now. Giving each core its own descriptor (and its own TSS) is what lets
// it take a ring3 -> ring0 transition, i.e. run user processes.
#define GDT_ENTRIES (5 + 2 * MAX_CPUS)
static uint64_t gdt[GDT_ENTRIES];

// Selector of CPU n's TSS descriptor. CPU 0 lands on the historical TSS_SEL.
#define TSS_SEL_FOR(cpu) (uint16_t)((5 + 2 * (cpu)) << 3)

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_ptr gp;

static tss_entry_t tss[MAX_CPUS];

// Build a 64-bit descriptor value:
//   access = access byte (e.g. 0x9A for kernel code)
//   flags  = nibble (e.g. 0x2 for long mode, 0x0 for data)
static uint64_t make_desc(uint8_t access, uint8_t flags) {
    uint64_t v = 0;
    v |= (uint64_t)access << 40;
    v |= (uint64_t)flags  << 52;
    return v;
}

// RSP0 is where the CPU lands on a ring3 -> ring0 transition, so it must name
// the kernel stack of the process THIS core is running. Per-CPU accordingly:
// the scheduler on each core sets its own.
void tss_set_stack(uint64_t rsp0) {
    cpu_info_t* me = cpu_self();
    tss[me->cpu_number < MAX_CPUS ? me->cpu_number : 0].rsp0 = rsp0;
}

// IST stacks are set once at boot, before any AP could fault, and are written
// into EVERY core's TSS. They are genuinely shared: two cores taking a double
// fault at the same instant would collide on one stack. That is an accepted
// limit of an already-fatal path, not something the scheduler can hit.
void tss_set_ist(uint8_t ist_idx, uint64_t stack_top) {
    for (int c = 0; c < MAX_CPUS; c++) {
        switch (ist_idx) {
            case 1: tss[c].ist1 = stack_top; break;
            case 2: tss[c].ist2 = stack_top; break;
            case 3: tss[c].ist3 = stack_top; break;
            case 4: tss[c].ist4 = stack_top; break;
            case 5: tss[c].ist5 = stack_top; break;
            case 6: tss[c].ist6 = stack_top; break;
            case 7: tss[c].ist7 = stack_top; break;
        }
    }
}

void load_tss(void) {
    __asm__ volatile("ltr %%ax" : : "a"(TSS_SEL));
}

// Each core loads ITS OWN TSS descriptor. Calling this with another core's
// selector would #GP on the second `ltr` (the descriptor is already busy).
void load_tss_for_cpu(uint32_t cpu) {
    if (cpu >= MAX_CPUS) return;
    __asm__ volatile("ltr %%ax" : : "a"(TSS_SEL_FOR(cpu)));
}

void init_gdt(void) {
    gp.limit = (sizeof(uint64_t) * GDT_ENTRIES) - 1; // 5 flat + one 16-byte TSS per CPU
    gp.base  = (uint64_t)&gdt + KERNEL_BASE;

    gdt[0] = 0;                                // null
    gdt[1] = make_desc(0x9A, 0x2);             // kernel code 64 (L-bit)
    gdt[2] = make_desc(0x92, 0x0);             // kernel data
    gdt[3] = make_desc(0xFA, 0x2);             // user code 64
    gdt[4] = make_desc(0xF2, 0x0);             // user data

    memset_asm(tss, 0, sizeof(tss));

    // One 16-byte TSS descriptor per CPU, at gdt[5+2c] / gdt[6+2c].
    for (int c = 0; c < MAX_CPUS; c++) {
        tss[c].iomap_base = sizeof(tss_entry_t);

        // Higher-half address: the TSS must be readable under a USER CR3, where
        // the kernel exists only through the PML4[511] mirror.
        uint64_t tss_base = (uint64_t)&tss[c] + KERNEL_BASE;
        uint32_t tss_limit = sizeof(tss_entry_t) - 1;

        uint64_t desc1 = 0;
        desc1 |= (tss_limit & 0xFFFF);
        desc1 |= (tss_base & 0xFFFFFF) << 16;      // base[23:0]
        desc1 |= (uint64_t)0x89 << 40;             // access
        desc1 |= (uint64_t)((tss_limit >> 16) & 0x0F) << 48;
        desc1 |= (uint64_t)((tss_base >> 24) & 0xFF) << 56;

        gdt[5 + 2 * c] = desc1;
        gdt[6 + 2 * c] = (tss_base >> 32) & 0xFFFFFFFF;
    }

    __asm__ volatile("lgdt %0" : : "m"(gp));
    load_tss();
}

// Point an application processor at the BSP's GDT. The descriptor table is
// read-only once built, so all CPUs can share one — and they must, since the
// IDT's gates name selector 0x08 and every CPU has to resolve that identically.
//
// The TSS is deliberately NOT loaded. An AP never leaves ring 0 yet, so it has
// no use for RSP0, and `ltr` on the shared TSS would #GP anyway: the BSP's ltr
// already set its busy bit, and a TSS may only be busy on one CPU. Running user
// processes on APs means giving each one its own TSS first.
void gdt_load_on_ap(void) {
    __asm__ volatile("lgdt %0" : : "m"(gp));
}
