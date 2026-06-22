#include "kernel.h"

#define IDENTITY_MAP_MB 64

static uint32_t* current_page_directory = NULL;
static uint32_t* kernel_page_directory = NULL;

void init_paging(void) {
    printf("[PAGING] Allocating page directory...\n");
    kernel_page_directory = (uint32_t*)alloc_page();
    if (!kernel_page_directory) kernel_panic("No page directory");
    memset_asm(kernel_page_directory, 0, PAGE_SIZE);
    printf("[PAGING] Page directory at %x\n", kernel_page_directory);

    int num_tables = IDENTITY_MAP_MB / 4;
    printf("[PAGING] Identity-mapping %d MB (%d page tables)...\n", IDENTITY_MAP_MB, num_tables);
    for (int t = 0; t < num_tables; t++) {
        uint32_t* pt = (uint32_t*)alloc_page();
        if (!pt) kernel_panic("No page table");
        for (uint32_t i = 0; i < 1024; i++) {
            pt[i] = ((t * 0x400000) + (i * PAGE_SIZE)) | 3;
        }
        kernel_page_directory[t] = (uint32_t)pt | 3;
    }

    printf("[PAGING] Loading CR3 with %x\n", kernel_page_directory);
    write_cr3((uint32_t)kernel_page_directory);

    printf("[PAGING] Enabling paging...\n");
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("jmp 1f\n1:");

    current_page_directory = kernel_page_directory;
    printf("[PAGING] Enabled successfully.\n");
}

void* get_phys_addr(void* virtual_addr) {
    uint32_t pd_index = (uint32_t)virtual_addr >> 22;
    uint32_t pt_index = ((uint32_t)virtual_addr >> 12) & 0x3FF;
    uint32_t offset = (uint32_t)virtual_addr & 0xFFF;
    if (!(kernel_page_directory[pd_index] & 1)) return NULL;
    uint32_t* pt = (uint32_t*)(kernel_page_directory[pd_index] & ~0xFFF);
    if (!(pt[pt_index] & 1)) return NULL;
    return (void*)((pt[pt_index] & ~0xFFF) + offset);
}

void map_page(void* phys, void* virt, uint32_t flags) {
    (void)phys; (void)virt; (void)flags;
    uint32_t pd_idx = (uint32_t)virt >> 22;
    uint32_t pt_idx = ((uint32_t)virt >> 12) & 0x3FF;
    if (!(kernel_page_directory[pd_idx] & 1)) {
        uint32_t* pt = (uint32_t*)alloc_page();
        if (!pt) return;
        memset_asm(pt, 0, PAGE_SIZE);
        kernel_page_directory[pd_idx] = (uint32_t)pt | 3;
    }
    uint32_t* pt = (uint32_t*)(kernel_page_directory[pd_idx] & ~0xFFF);
    pt[pt_idx] = ((uint32_t)phys & ~0xFFF) | (flags & 0xFFF) | 1;
    __asm__ volatile("invlpg (%0)" :: "r"(virt));
}

void unmap_page(void* virt) {
    uint32_t pd_idx = (uint32_t)virt >> 22;
    uint32_t pt_idx = ((uint32_t)virt >> 12) & 0x3FF;
    if (!(kernel_page_directory[pd_idx] & 1)) return;
    uint32_t* pt = (uint32_t*)(kernel_page_directory[pd_idx] & ~0xFFF);
    pt[pt_idx] = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virt));
}

void* clone_page_directory(void) {
    return NULL;
}
