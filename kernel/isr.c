#include "kernel.h"

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

static void* isr_stubs[32] = {
    isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9,
    isr10, isr11, isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19,
    isr20, isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29,
    isr30, isr31
};

static const char* exception_names[32] = {
    "Division by Zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD FPU Exception",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security Exception", "Reserved"
};

void isr_handler(uint64_t int_no) {
    if (int_no < 32) {
        printf("\n[PANIC] Exception: %s\n", exception_names[int_no]);
        if (int_no == 14) {
            uint64_t cr2 = read_cr2();
            printf("[PANIC] Page fault at 0x%lx\n", cr2);
        }
        kernel_panic("Unhandled exception");
    }
}

void init_isr(void) {
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint64_t)isr_stubs[i] + KERNEL_BASE, 0x08, 0x8E);
    }
}
