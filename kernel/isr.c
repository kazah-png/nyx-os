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

extern int vm_handle_fault(uint64_t cr2, uint64_t err);

// Which POSIX signal a real kernel delivers for a given CPU exception vector.
static int exception_signal(uint64_t int_no) {
    switch (int_no) {
        case 0:  return SIGFPE;    // #DE divide error
        case 6:  return SIGILL;    // #UD invalid opcode
        case 16: return SIGFPE;    // #MF x87 FP error
        case 19: return SIGFPE;    // #XM SIMD FP
        default: return SIGSEGV;   // #GP, #PF, #SS, #NP, … — memory/protection faults
    }
}

void isr_handler(uint64_t int_no, uint64_t rip, uint64_t error, uint64_t cs) {
    // Page fault: give demand-paging / copy-on-write a chance to resolve it
    // (allocate the page / make a private copy) and retry the instruction.
    if (int_no == 14 && vm_handle_fault(read_cr2(), error))
        return;

    if (int_no < 32) {
        uint64_t cr2 = (int_no == 14) ? read_cr2() : 0;

        // A fault taken from RING 3 must NOT bring down the whole kernel: terminate
        // just the offending process — the default action of the fatal signal it
        // earned — and yield to the scheduler forever, exactly like SYS_EXIT. This
        // is what turns a crashing user program from a system-wide panic into a
        // recoverable "process killed" (the shell's other jobs keep running).
        if ((cs & 3) == 3) {
            int signo = exception_signal(int_no);
            process_t* cur = get_current_process();
            printf("\n[fault] pid %u (%s): %s (#%lu) at RIP 0x%lx err 0x%lx",
                   cur ? (unsigned)cur->pid : 0, cur ? cur->comm : "?",
                   exception_names[int_no], int_no, rip, error);
            if (int_no == 14) printf(" fault-addr 0x%lx", cr2);
            printf(" -> killed (signal %d)\n", signo);
            if (cur) {
                cur->exit_code = 128 + signo;   // waitpid status convention: 128 + signo
                close_proc_fds(cur);            // drop pipe ends so readers get EOF
                cur->state = PROC_ZOMBIE;
                wake_waiters(cur);              // unblock a parent in waitpid()
            }
            __asm__ volatile("sti");            // hand the CPU to the scheduler and
            for (;;) __asm__ volatile("hlt");   // never resume the faulting instruction
        }

        // A fault in RING 0 is a genuine kernel bug — unrecoverable, so panic.
        printf("\n[PANIC] Exception: %s (#%lu)\n", exception_names[int_no], int_no);
        printf("[PANIC] RIP=0x%lx  CS=0x%lx (ring %lu)  error=0x%lx\n",
               rip, cs, cs & 3, error);
        if (int_no == 14) printf("[PANIC] Page fault at 0x%lx\n", cr2);
        kernel_panic("%s (#%lu) at RIP 0x%lx (ring %lu, err 0x%lx)",
                     exception_names[int_no], int_no, rip, cs & 3, error);
    }
}

void init_isr(void) {
    for (int i = 0; i < 32; i++) {
        // Use IST1 for Double Fault (#8) to prevent triple faults
        if (i == 8) {
            idt_set_gate_ist(i, (uint64_t)isr_stubs[i] + KERNEL_BASE, 0x08, 0x8E, IST_DOUBLE_FAULT);
        } else {
            idt_set_gate(i, (uint64_t)isr_stubs[i] + KERNEL_BASE, 0x08, 0x8E);
        }
    }
}
