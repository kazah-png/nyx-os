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
    ; Save interrupted registers FIRST — the CR3 switch below clobbers rax, and a
    ; handler that returns (e.g. demand paging) would otherwise resume with a
    ; corrupted rax (same class of bug as the old irq_common). Exceptions that
    ; panic don't care, but this keeps the path correct for recoverable faults.
    SAVE_REGS
    ; Remember the FAULTING CR3. r15 is callee-saved, so the C isr_handler
    ; preserves it across the call. A recoverable fault (demand paging / COW)
    ; must iretq back on the address space that faulted — for a ring-3 fault
    ; that's the process's own CR3, not the kernel CR3 we switch to just below.
    ; Restored right before RESTORE_REGS/iretq (RESTORE_REGS then reloads the
    ; user's saved r15 off the stack).
    mov r15, cr3
    ; Switch to kernel page tables in case we came from user mode (rax is now
    ; saved scratch, restored by RESTORE_REGS).
    mov rax, KERNEL_BASE + kernel_pml4_phys
    mov rax, [rax]
    test rax, rax
    jz .isr_bad
    test rax, 0xFFF
    jnz .isr_bad
    mov cr3, rax
    jmp .isr_done
.isr_bad:
    mov dx, 0x3FD
.w1: in al, dx; test al, 0x20; jz .w1
    mov dx, 0x3F8; mov al, 'I'; out dx, al
    mov dx, 0x3FD
.w2: in al, dx; test al, 0x20; jz .w2
    mov dx, 0x3F8; mov al, 'C'; out dx, al
    mov dx, 0x3FD
.w3: in al, dx; test al, 0x20; jz .w3
    mov dx, 0x3F8; mov al, '3'; out dx, al
.isr_halt: cli; hlt; jmp .isr_halt
.isr_done:
    ; Stack: [RSP+0]=R15, ..., [RSP+112]=RAX
    ; After SAVE_REGS: [RSP+120]=int_no, [RSP+128]=error, [RSP+136]=RIP, [RSP+144]=CS, [RSP+152]=RFLAGS, ...
    mov rdi, [rsp + 120]     ; int_no
    mov rsi, [rsp + 136]     ; faulting RIP
    mov rdx, [rsp + 128]     ; error code
    mov rcx, [rsp + 144]     ; CS (ring is CS & 3)
    mov r8, rsp              ; 5th arg: exception frame base (GPRs..int/err/RIP/CS/RFLAGS/RSP/SS)
    call isr_handler         ; may rewrite the frame to divert a ring-3 fault into a signal handler
    mov cr3, r15             ; restore the faulting address space before returning
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
global syscall_frame_ptr
extern syscall_handler
extern signal_dispatch

section .data
user_rsp:  dq 0
kernel_rsp: dq 0
user_cr3:  dq 0
; Base of the saved user register frame on the kernel stack during a syscall:
; [+0..112]=GPRs (r15..rax), [+120]=RFLAGS, [+128]=RIP. do_fork() reads it to
; clone the caller's ring-3 context into the child (with rax=0 for the child).
syscall_frame_ptr: dq 0

section .text
; Entry: rax=syscall#, rdi/rsi/rdx/r10/r8/r9=args, rcx=user RIP, r11=user RFLAGS,
; rsp=USER stack, CR3=USER. Interrupts are masked (SF_MASK clears IF).
; LSTAR is the higher-half alias of this label, so RIP is in the higher half and
; every `default rel` memory access resolves to the higher-half alias of our
; .data — which the user CR3 maps via PML4[511]. That lets us stash RSP and save
; all user GPRs onto the kernel stack with NO scratch register, before touching
; CR3. (The previous version clobbered RAX/RBX during the CR3 switch *before*
; SAVE_REGS, corrupting the syscall number and the user's RBX/RSP.)
syscall_entry:
    mov [user_rsp], rsp          ; stash user RSP (higher-half alias, mapped in user CR3)
    mov rsp, [kernel_rsp]        ; switch to kernel stack (stored as a higher-half alias)
    push rcx                     ; user return RIP
    push r11                     ; user RFLAGS
    SAVE_REGS                    ; save all 15 user GPRs intact — nothing clobbered yet

    ; All user state is now on the (higher-half) kernel stack; rax/rbx are free.
    mov rax, cr3
    mov [user_cr3], rax          ; save user CR3
    mov rax, [kernel_pml4_phys]
    mov cr3, rax                 ; -> kernel CR3 (higher-half stack still mapped via 511)

    ; Marshal saved user regs into SysV C argument registers.
    ; Stack: [RSP+0..112]=GPRs (r15..rax), [RSP+120]=RFLAGS, [RSP+128]=RIP
    mov rdi, [rsp + 112]         ; RAX = syscall number
    mov rsi, [rsp + 72]          ; RDI = arg1
    mov rdx, [rsp + 80]          ; RSI = arg2
    mov rcx, [rsp + 88]          ; RDX = arg3
    mov r8,  [rsp + 40]          ; R10 = arg4
    mov r9,  [rsp + 56]          ; R8  = arg5
    mov [syscall_frame_ptr], rsp ; expose the saved user frame to do_fork() (frame base)
    ; arg6 (user R9, at frame+48) is the 7th integer arg -> passed on the stack per
    ; SysV. Push it so the callee finds it just above the return address, then pop it
    ; back after the call so rsp is the frame base again for the RAX-slot write below.
    ; (Alignment isn't required: the kernel is built -mno-sse, so no movaps.)
    push qword [rsp + 48]        ; arg6
    call syscall_handler
    add rsp, 8                   ; drop arg6 -> rsp back at the frame base
    mov [rsp + 112], rax         ; return value -> saved RAX slot

    ; Deliver a pending signal (still on the kernel CR3; user_cr3 valid for the user
    ; stack write). May rewrite the frame (RIP->handler, RDI->signo) + user_rsp, or
    ; not return at all for a default-terminate signal. rsp = the saved user frame.
    mov rdi, rsp
    call signal_dispatch

    ; Return: back to user CR3 first (higher-half stack stays mapped), then restore.
    mov rax, [user_cr3]
    mov cr3, rax
    RESTORE_REGS                 ; restore user GPRs (RAX = return value)
    pop r11                      ; user RFLAGS
    pop rcx                      ; user RIP
    ; Return to ring 3 via iretq (NASM's bare `sysret` is the 32-bit form — it
    ; drops to compat mode and truncates RSP to 32 bits; iretq is how the scheduler
    ; also enters ring 3, with no STAR/GDT selector-ordering constraints). Build the
    ; iret frame: SS, RSP, RFLAGS, CS, RIP.
    push 0x23                    ; user SS (USER_DS, RPL 3)
    push qword [user_rsp]        ; user RSP
    push r11                     ; RFLAGS
    push 0x1B                    ; user CS (USER_CS, RPL 3)
    push rcx                     ; user RIP
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

irq_common:
    ; Save ALL interrupted registers FIRST, before touching rax/rbx. The CR3
    ; switch below used rax/rbx as scratch *before* SAVE_REGS, so on return the
    ; interrupted code got a corrupted RBX (= KERNEL_BASE) / RAX and, if it then
    ; computed a call/jump from them, jumped to KERNEL_BASE+3 → #UD. This was the
    ; intermittent GUI/keyboard crash and the timer/scheduler blocker.
    SAVE_REGS
    ; Switch to kernel page tables for C code. rax/rbx are now free scratch — they
    ; are saved on the stack and restored by RESTORE_REGS before iretq.
    mov rax, kernel_pml4_phys
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rax, [rax]
    test rax, rax
    jz .irq_bad
    test rax, 0xFFF
    jnz .irq_bad
    mov cr3, rax
    jmp .irq_done
.irq_bad:
    mov dx, 0x3FD
.w4: in al, dx; test al, 0x20; jz .w4
    mov dx, 0x3F8; mov al, 'K'; out dx, al
    mov dx, 0x3FD
.w5: in al, dx; test al, 0x20; jz .w5
    mov dx, 0x3F8; mov al, '3'; out dx, al
.irq_halt: cli; hlt; jmp .irq_halt
.irq_done:
    ; Stack: [RSP+0]=R15..[RSP+112]=RAX, [RSP+120]=int_no, [RSP+128]=error, [RSP+136]=RIP
    mov rdi, [rsp + 120]     ; int_no
    call irq_handler
    ; Send EOI (handles APIC or legacy PIC)
    mov rdi, [rsp + 120]     ; int_no
    call irq_eoi
    ; Scheduler tick — save RSP via higher-half address
    mov rax, saved_rsp
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov [rax], rsp
    call irq_scheduler_tick
    ; Read next RSP (higher-half address, valid in any CR3 via PML4[511] mirror)
    mov rax, next_rsp
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rdx, [rax]               ; rdx = next_rsp (higher-half address)
    ; Set RSP before switching CR3 — ensures valid stack during CR3 switch
    mov rsp, rdx
    ; Validate next_cr3 before switching
    mov rax, next_cr3
    mov rbx, KERNEL_BASE
    add rax, rbx
    mov rax, [rax]
    test rax, rax
    jz .bad_cr3
    test rax, 0xFFF
    jnz .bad_cr3
    mov cr3, rax
    jmp .restore_done
.bad_cr3:
    mov dx, 0x3FD
.b1: in al, dx; test al, 0x20; jz .b1
    mov dx, 0x3F8; mov al, 'B'; out dx, al
    mov dx, 0x3FD
.b2: in al, dx; test al, 0x20; jz .b2
    mov dx, 0x3F8; mov al, '3'; out dx, al
.b3: cli; hlt; jmp .b3
.restore_done:
    RESTORE_REGS
    add rsp, 16
    iretq

; ---------------------------------------------------------------------------
; Application-processor LAPIC timer (vector AP_TIMER_VECTOR).
;
; Deliberately NOT irq_common. irq_common drives the scheduler through the
; globals saved_rsp / next_rsp / next_cr3 — ONE set for the whole machine — so
; an AP entering it would overwrite whatever context switch the BSP was in the
; middle of. Until the scheduler is per-CPU and spinlocked, an AP may only touch
; its own cpu_info slot, which is all ap_timer_tick does.
;
; No CR3 juggling either: an AP always runs in the kernel address space, so the
; page tables it was interrupted under are already the ones C code needs.
;
; Only the SysV call-clobbered registers are saved — ap_timer_tick preserves the
; rest by ABI. 9 pushes on top of the CPU's 5-qword frame leave RSP 16-byte
; aligned at the `call`, as the ABI requires.
; ---------------------------------------------------------------------------
extern ap_timer_tick

global ap_timer_stub
ap_timer_stub:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    call ap_timer_tick
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq

; Spurious LAPIC interrupt. It takes no EOI by design — the only job here is to
; exist, so a spurious vector can't fault an AP into a triple fault.
global ap_spurious_stub
ap_spurious_stub:
    iretq
