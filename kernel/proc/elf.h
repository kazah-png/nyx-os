#ifndef ELF_H
#define ELF_H

#define EI_NIDENT 16

/* ELF64 header */
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_hdr_t;

/* ELF64 program header */
typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

#define ELF_MAGIC       0x464C457F  /* "\x7fELF" */
#define ELF_64BIT       2
#define ELF_LITTLE_ENDIAN 1
#define EM_X86_64       62
#define ELF_EXEC        2

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4

#define PF_X 1
#define PF_W 2
#define PF_R 4

/* ELF64 section header + symbol (used by dlsym to resolve a name to a VA). */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed)) elf64_shdr_t;

typedef struct {
    uint32_t st_name;      /* index into the linked string table */
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;     /* the symbol's virtual address (prelinked libs) */
    uint64_t st_size;
} __attribute__((packed)) elf64_sym_t;

#define SHT_SYMTAB 2
#define SHT_STRTAB 3

#endif
