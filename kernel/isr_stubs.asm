; isr_stubs.asm - x86_64 interrupt stubs for NyxOS
default rel
BITS 64

; Higher-half kernel offset for accessing variables from user page tables
%define KERNEL_BASE 0xFFFFFF8000000000

extern kernel_pml4_phys
extern next_cr3
extern saved_rsp
extern next_rsp
extern irq_eoi

; Save all registers (15 pushes)
%macro SAVE_REGS 0
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro RESTORE_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

; ISR with no error code
%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

extern isr_handler
isr_common:
    ; Switch to kernel page tables in case we came from user mode
    mov rax, KERNEL_BASE + kernel_pml4_phys
    mov rax, [rax]
    mov cr3, rax
    SAVE_REGS
    ; Stack: [RSP+0]=R15, ..., [RSP+112]=RAX
    ; After SAVE_REGS: [RSP+120]=int_no, [RSP+128]=error, [RSP+136]=RIP, [RSP+144]=CS, [RSP+152]=RFLAGS, ...
    mov rdi, [rsp + 120]     ; int_no
    call isr_handler
    RESTORE_REGS
    add rsp, 16               ; pop error code + int number
    iretq

; Syscall entry via syscall instruction
; RAX = syscall number, RDI/RSI/RDX/R10/R8/R9 = args (Linux convention)
; RCX = return RIP (set by syscall), R11 = saved RFLAGS (set by syscall)
global syscall_entry
global user_rsp
global kernel_rsp
global user_cr3
extern syscall_handler

section .data
user_rsp:  dq 0
kernel_rsp: dq 0
user_cr3:  dq 0

section .text
syscall_entry:
    ; Save user CR3 and RSP before switching to kernel page tables
    ; User CR3 maps higher half via PML4[256], so use higher-half addressing

    mov rax, user_cr3
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rbx, cr3
    mov [rax], rbx

    mov rax, user_rsp
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov [rax], rsp

    ; Switch to kernel page tables
    mov rax, kernel_pml4_phys
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rax, [rax]
    mov cr3, rax

    ; Switch to kernel stack (identity address, now accessible)
    mov rsp, [kernel_rsp]
    push rcx                     ; return RIP
    push r11                     ; return RFLAGS
    SAVE_REGS
    ; Stack: [RSP+0..112]=regs, [RSP+120]=RFLAGS, [RSP+128]=RIP
    mov rdi, [rsp + 112]        ; RAX = syscall number
    mov rsi, [rsp + 72]         ; RDI = arg1
    mov rdx, [rsp + 80]         ; RSI = arg2
    mov rcx, [rsp + 88]         ; RDX = arg3
    mov r8,  [rsp + 40]         ; R10 = arg4
    mov r9,  [rsp + 56]         ; R8  = arg5
    call syscall_handler
    mov [rsp + 112], rax        ; save return value in saved RAX slot
    RESTORE_REGS
    pop r11                      ; restore RFLAGS
    pop rcx                      ; restore return RIP
    ; Switch back to user page tables
    mov rax, user_cr3
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rax, [rax]
    mov cr3, rax
    mov rax, user_rsp
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rsp, [rax]
    sysret

; IRQ stubs (mapped to INT 32-47)
%macro IRQ 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_common
%endmacro

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

extern irq_handler
extern irq_scheduler_tick

irq_common:
    ; Switch to kernel page tables immediately
    ; CPU already switched to kernel stack (via TSS.RSP0 = higher half addr)
    ; and pushed iretq frame. We're running from higher half address.
    mov rax, kernel_pml4_phys
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rax, [rax]
    mov cr3, rax

    SAVE_REGS
    ; Stack: [RSP+0]=R15..[RSP+112]=RAX, [RSP+120]=int_no, [RSP+128]=error, [RSP+136]=RIP
    mov rdi, [rsp + 120]     ; int_no
    call irq_handler
    ; Send EOI (handles APIC or legacy PIC)
    mov rdi, [rsp + 120]     ; int_no
    call irq_eoi
    ; Scheduler tick
    mov [saved_rsp], rsp
    call irq_scheduler_tick
    ; Read next RSP *before* switching to user page tables (no identity mapping)
    mov rax, next_rsp
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rdx, [rax]               ; rdx = next_rsp (user kernel stack, higher-half address)
    ; Switch to next process page tables
    mov rax, next_cr3
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rax, [rax]
    mov cr3, rax
    mov rsp, rdx                 ; Use the value we saved before the CR3 switch
    RESTORE_REGS
    add rsp, 16
    iretq

; Trampoline for direct user process launch from kernel code
; Called via higher-half address (indirect call through register)
; rdi = proc->stack value (raw, identity-mapped → converted to higher-half)
; rsi = proc->page_directory (physical address of user PML4)
global switch_to_user_trampoline
switch_to_user_trampoline:
    mov rax, KERNEL_BASE
    add rdi, rax            ; convert RSP to higher-half address
    mov rsp, rdi            ; switch to user's kernel stack (higher-half)
    mov cr3, rsi            ; switch to user page tables (physical)
    RESTORE_REGS
    add rsp, 16
    iretq
