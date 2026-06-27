OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
    data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}

SECTIONS {
    . = 0x100000;

    .text : ALIGN(16) {
        KEEP(*(.multiboot))
        *(.text)
    } :text

    .rodata : ALIGN(16) {
        *(.rodata)
    } :data

    .data : ALIGN(16) {
        *(.data)
    } :data

    .bss : ALIGN(4096) {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } :data

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
