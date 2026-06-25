#include "kernel.h"

// x86_64 IDT entry: 16 bytes
struct idt_entry {
    uint16_t offset_low;    // bits 0-15
    uint16_t selector;
    uint8_t ist;            // bits 0-2 = IST index
    uint8_t type_attr;
    uint16_t offset_mid;    // bits 16-31
    uint32_t offset_high;   // bits 32-63
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_mid  = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].reserved    = 0;
}

void init_idt(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt;

    memset_asm(&idt, 0, sizeof(idt));

    _idt_flush((uint64_t)&idtp);
}
