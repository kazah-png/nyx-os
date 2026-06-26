#include "kernel.h"

// AMD64 GDT descriptor format — use raw 64-bit values
// Standard entries: 8 bytes (1 qword)
// TSS descriptor: 16 bytes (2 qwords)
#define GDT_ENTRIES 7
static uint64_t gdt[GDT_ENTRIES];

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_ptr gp;

static tss_entry_t tss;

// Build a 64-bit descriptor value:
//   access = access byte (e.g. 0x9A for kernel code)
//   flags  = nibble (e.g. 0x20 for long mode, 0x00 for data)
static uint64_t make_desc(uint8_t access, uint8_t flags) {
    uint64_t v = 0;
    v |= (uint64_t)access << 40;
    v |= (uint64_t)flags  << 52;
    return v;
}

void tss_set_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void load_tss(void) {
    __asm__ volatile("ltr %%ax" : : "a"(TSS_SEL));
}

void init_gdt(void) {
    gp.limit = (sizeof(uint64_t) * 6 + 16) - 1; // 6 standard + 1 TSS (16 bytes)
    gp.base  = (uint64_t)&gdt + KERNEL_BASE;

    gdt[0] = 0;                                // null
    gdt[1] = make_desc(0x9A, 0x20);            // kernel code 64 (L-bit)
    gdt[2] = make_desc(0x92, 0x00);            // kernel data
    gdt[3] = make_desc(0xFA, 0x20);            // user code 64
    gdt[4] = make_desc(0xF2, 0x00);            // user data

    memset_asm(&tss, 0, sizeof(tss));
    tss.iomap_base = sizeof(tss);

    // Use higher-half address so TSS is accessible from user CR3
    uint64_t tss_base = (uint64_t)&tss + KERNEL_BASE;
    uint32_t tss_limit = sizeof(tss) - 1;

    // Build 16-byte TSS descriptor at gdt[5..6]
    uint64_t desc1 = 0;
    desc1 |= (tss_limit & 0xFFFF);
    desc1 |= (tss_base & 0xFFFFFF) << 16;      // base[23:0]
    desc1 |= (uint64_t)0x89 << 40;             // access
    desc1 |= (uint64_t)((tss_limit >> 16) & 0x0F) << 48;
    desc1 |= (uint64_t)((tss_base >> 24) & 0xFF) << 56;

    uint64_t desc2 = (tss_base >> 32) & 0xFFFFFFFF;

    gdt[5] = desc1;
    gdt[6] = desc2;

    __asm__ volatile("lgdt %0" : : "m"(gp));
    load_tss();
}
