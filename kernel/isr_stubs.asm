; isr_stubs.asm - x86_64 interrupt stubs for NyxOS
default rel
BITS 64

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
    SAVE_REGS
    ; Stack: [RSP+0]=R15, ..., [RSP+112]=RAX
    ; After error+int: [RSP+120]=error, [RSP+128]=int_no, [RSP+136]=RIP, [RSP+144]=CS, [RSP+152]=RFLAGS, ...
    mov rdi, [rsp + 128]     ; int_no
    call isr_handler
    RESTORE_REGS
    add rsp, 16               ; pop error code + int number
    iretq

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
extern saved_rsp
extern next_rsp
irq_common:
    SAVE_REGS
    mov rdi, [rsp + 128]     ; int_no
    call irq_handler
    ; Send EOI
    mov al, 0x20
    cmp byte [rsp + 128], 40
    jb .master_only
    out 0xA0, al
.master_only:
    out 0x20, al
    ; Scheduler tick
    mov [saved_rsp], rsp
    call irq_scheduler_tick
    mov rsp, [next_rsp]
    RESTORE_REGS
    add rsp, 16
    iretq

; Syscall entry via syscall instruction
; RAX = syscall number, RDI/RSI/RDX/R10/R8/R9 = args (Linux convention)
; RCX = return RIP (set by syscall), R11 = saved RFLAGS (set by syscall)
global syscall_entry
global user_rsp
global kernel_rsp
extern syscall_handler

section .data
user_rsp:  dq 0
kernel_rsp: dq 0

section .text
syscall_entry:
    ; Save user RSP, switch to kernel stack
    mov [user_rsp], rsp
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
    mov rsp, [user_rsp]          ; restore user RSP
    sysret
