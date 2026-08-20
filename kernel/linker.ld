OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* PF_R | PF_X */
    data PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}

SECTIONS {
    . = 0x100000;

    _text_start = .;
    .text : ALIGN(16) {
        KEEP(*(.multiboot))
        *(.text .text.* .ltext .ltext.*)    /* gather per-function (.text.*) code too, so
                                               _text_end spans ALL code (panic backtrace) */
    } :text
    _text_end = .;

    .rodata : ALIGN(16) {
        *(.rodata)
        *(.rodata.*)
        *(.lrodata)             /* large-model (-mcmodel=large) rodata */
        *(.lrodata.*)
    } :data

    .data : ALIGN(16) {
        *(.data)
        *(.data.*)
        *(.ldata)               /* large-model data */
        *(.ldata.*)
    } :data

    .bss : ALIGN(4096) {
        *(.bss)
        *(.bss.*)
        /* -mcmodel=large puts large objects in .lbss/.ldata/.lrodata. Without
         * capturing .lbss here, the linker emits page_refcount[]/page_pinned[] (the
         * allocator's own metadata) AFTER _kernel_end, so init_memory never reserves
         * their frames — alloc_page then hands them out and the page-zeroing memset
         * wipes the refcounts, double-allocating live pages: THE pipeline corruption. */
        *(.lbss)
        *(.lbss.*)
        *(COMMON)
    } :data

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
