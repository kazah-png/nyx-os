#include "kernel.h"

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

static void (*irq_handlers[16])(void*) = {0};

void init_irq(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    idt_set_gate(32, (uint64_t)irq0  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9  + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10 + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11 + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12 + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13 + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14 + KERNEL_BASE, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15 + KERNEL_BASE, 0x08, 0x8E);
}

void irq_handler(uint64_t int_no) {
    uint32_t irq_no = (uint32_t)(int_no - 32);
    if (irq_no == 0) tick_count++;
    if (irq_handlers[irq_no] != NULL) {
        irq_handlers[irq_no](NULL);
    }
}

void irq_install_handler(int irq, void (*handler)(void*)) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_unmask(int irq) {
    if (irq < 8) {
        outb(0x21, inb(0x21) & ~(1 << irq));
    } else {
        outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
    }
}

void irq_mask(int irq) {
    if (irq < 8) {
        outb(0x21, inb(0x21) | (1 << irq));
    } else {
        outb(0xA1, inb(0xA1) | (1 << (irq - 8)));
    }
}
