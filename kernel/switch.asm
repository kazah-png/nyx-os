; switch.asm - x86_64 context switch for NyxOS
default rel
BITS 64

global switch_context
global create_task_stack

; switch_context(uint64_t* old_rsp_ptr, uint64_t new_rsp)
; RDI = old_rsp_ptr, RSI = new_rsp (x86_64 ABI)
switch_context:
    push rbx
    push rcx
    push rdx
    push rsi           ; saved RSI = new_rsp
    push rdi           ; saved RDI = old_rsp_ptr
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    ; [RSP+0]=r15, [RSP+8]=r14, ..., [RSP+96]=rdi, [RSP+104]=rsi
    ; Read saved args from stack
    mov rax, [rsp + 96]        ; old_rsp_ptr
    mov [rax], rsp             ; *old_rsp_ptr = current RSP
    mov rsp, [rsp + 104]       ; RSP = new_rsp
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
    ret

; create_task_stack(uint64_t stack_top, uint64_t entry_point)
; RDI = stack_top, RSI = entry_point
create_task_stack:
    mov rax, rdi                ; stack_top
    mov rcx, rsi                ; entry_point
    mov [rax - 8], rcx          ; return address = entry_point
    mov qword [rax - 16], 0     ; r15
    mov qword [rax - 24], 0     ; r14
    mov qword [rax - 32], 0     ; r13
    mov qword [rax - 40], 0     ; r12
    mov qword [rax - 48], 0     ; r11
    mov qword [rax - 56], 0     ; r10
    mov qword [rax - 64], 0     ; r9
    mov qword [rax - 72], 0     ; r8
    mov qword [rax - 80], 0     ; rbp
    mov qword [rax - 88], 0     ; rdi
    mov qword [rax - 96], 0     ; rsi
    mov qword [rax - 104], 0    ; rdx
    mov qword [rax - 112], 0    ; rcx
    mov qword [rax - 120], 0    ; rbx
    mov qword [rax - 128], 0    ; rax
    lea rax, [rax - 128]
    ret
