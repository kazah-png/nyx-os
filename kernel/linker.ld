ENTRY(_start)

SECTIONS {
    . = 0x100000;

    .text : ALIGN(4) {
        KEEP(*(.multiboot))
        *(.text)
    }

    .data : ALIGN(4) {
        *(.data)
        *(.rodata)
    }

    .bss : ALIGN(4) {
        *(.bss)
        *(COMMON)
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}