; CRT0 for NyxOS x86_64 user programs
; Calls main(argc, argv) then exit()
BITS 64

global _start
extern main

section .text
_start:
    ; x86_64 ABI: RDI=argc, RSI=argv
    xor rdi, rdi    ; argc = 0
    xor rsi, rsi    ; argv = NULL
    call main

    ; exit(rax)
    mov rdi, rax
    mov rax, 0      ; SYS_EXIT
    syscall
    hlt
