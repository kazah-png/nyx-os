; AP boot trampoline — flat binary loaded at 0x8000
; SIPI vector = 8, physical entry = 0x8000

BITS 16
ORG 0x8000
DEFAULT ABS          ; explicit absolute addressing (flat binary)

; BSP must fill these before SIPI:
;   0x8FE0 — CR3 page-table physical address
;   0x8FE8 — stack pointer (RSP)
;   0x8FF0 — CPU ID
;   0x8FF8 — entry point (ap_main kernel virtual address)

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    lgdt [gdtr32]

    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:prot_mode

BITS 32
prot_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov eax, cr4
    or eax, (1 << 5)
    mov cr4, eax

    mov eax, [cr3_value]
    mov cr3, eax

    ; EFER: LME (long mode) + NXE (no-execute). NXE is per-CPU and the BSP sets
    ; its own in init_paging, so an AP that skipped it saw every PAGE_NX bit as a
    ; RESERVED bit — and faulted the instant it touched an NX page. That is silent
    ; while an AP only halts; it kills the core the moment it reads its own LAPIC
    ; (map_mmio maps that page NX).
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11)
    wrmsr

    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    lgdt [gdtr64]
    jmp 0x08:long_mode

BITS 64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, [stack_pointer]
    mov rbp, rsp
    mov edi, [cpu_id]
    mov rax, [entry_point]
    call rax

halt:
    cli
    hlt
    jmp halt

align 16
gdt32:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt32_end:

gdtr32:
    dw gdt32_end - gdt32 - 1
    dd gdt32

align 16
gdt64:
    dq 0x0000000000000000
    dq 0x00209A0000000000
    dq 0x0000920000000000
gdt64_end:

gdtr64:
    dw gdt64_end - gdt64 - 1
    dq gdt64

align 8
cr3_value:      dq 0
stack_pointer:  dq 0
cpu_id:         dd 0
pad:            dd 0
entry_point:    dq 0

end:
