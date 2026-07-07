; CRT0 for NyxOS x86_64 user programs
; Calls main(argc, argv) then exit()
BITS 64

global _start
extern main

section .text
_start:
    ; SysV-style entry stack, built by the kernel for every user launch:
    ;   [rsp]   = argc
    ;   [rsp+8] = argv[0..argc-1], NULL          (then an envp NULL)
    ; elf_load_image writes an empty frame (argc=0) for plain spawns;
    ; execve() builds the real one from the caller's argv.
    mov rdi, [rsp]      ; argc
    lea rsi, [rsp+8]    ; argv
    call main

    ; exit(rax)
    mov rdi, rax
    mov rax, 0      ; SYS_EXIT
    syscall
    hlt

; Signal-return trampoline. The kernel (signal_dispatch) pushes this address as the
; handler's return address; when the handler `ret`s, control lands here and
; SYS_SIGRETURN restores the interrupted context — so this does not return. The
; libc signal() wrapper hands the kernel this address as the sa_restorer.
global __sigreturn
__sigreturn:
    mov rax, 18     ; SYS_SIGRETURN
    syscall
    hlt             ; unreachable: the kernel iretq's back to the interrupted RIP
