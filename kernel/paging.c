#include "kernel.h"

#define IDENTITY_MAP_MB 64
#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITABLE   (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_HUGE       (1 << 7)

// PML4 indices
#define PML4_IDENTITY 0
#define PML4_HIGHER   256       // 0xFFFFFF8000000000

static uint64_t* current_pml4 = NULL;
static uint64_t* kernel_pml4 = NULL;

// Get physical address (virtual -> physical) using kernel page tables
void* get_phys_addr(void* virtual_addr) {
    uint64_t addr = (uint64_t)virtual_addr;
    int pml4_idx = (addr >> 39) & 0x1FF;
    int pdpt_idx = (addr >> 30) & 0x1FF;
    int pd_idx   = (addr >> 21) & 0x1FF;
    int pt_idx   = (addr >> 12) & 0x1FF;
    int offset   = addr & 0xFFF;

    if (!kernel_pml4) return NULL;
    uint64_t pml4e = kernel_pml4[pml4_idx];
    if (!(pml4e & PAGE_PRESENT)) return NULL;

    uint64_t* pdpt = (uint64_t*)(pml4e & ~0xFFF);
    uint64_t pdpte = pdpt[pdpt_idx];
    if (!(pdpte & PAGE_PRESENT)) return NULL;

    // 1GB huge page?
    if (pdpte & PAGE_HUGE) {
        return (void*)((pdpte & ~((1ULL << 30) - 1)) + (addr & ((1ULL << 30) - 1)));
    }

    uint64_t* pd = (uint64_t*)(pdpte & ~0xFFF);
    uint64_t pde = pd[pd_idx];
    if (!(pde & PAGE_PRESENT)) return NULL;

    // 2MB huge page?
    if (pde & PAGE_HUGE) {
        return (void*)((pde & ~((1ULL << 21) - 1)) + (addr & ((1ULL << 21) - 1)));
    }

    uint64_t* pt = (uint64_t*)(pde & ~0xFFF);
    uint64_t pte = pt[pt_idx];
    if (!(pte & PAGE_PRESENT)) return NULL;

    return (void*)((pte & ~0xFFF) + offset);
}

// Map a page in a specific PML4
static void map_pml4(uint64_t* pml4, void* phys, void* virt, uint32_t flags) {
    uint64_t vaddr = (uint64_t)virt;
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    int pd_idx   = (vaddr >> 21) & 0x1FF;
    int pt_idx   = (vaddr >> 12) & 0x1FF;

    uint64_t pml4e = pml4[pml4_idx];
    uint64_t* pdpt;
    if (!(pml4e & PAGE_PRESENT)) {
        pdpt = (uint64_t*)alloc_page();
        if (!pdpt) return;
        memset_asm(pdpt, 0, 4096);
        pml4[pml4_idx] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER ? PAGE_USER : 0);
    } else {
        pdpt = (uint64_t*)(pml4e & ~0xFFF);
    }

    uint64_t pdpte = pdpt[pdpt_idx];
    uint64_t* pd;
    if (!(pdpte & PAGE_PRESENT)) {
        pd = (uint64_t*)alloc_page();
        if (!pd) return;
        memset_asm(pd, 0, 4096);
        pdpt[pdpt_idx] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER ? PAGE_USER : 0);
    } else {
        pd = (uint64_t*)(pdpte & ~0xFFF);
    }

    uint64_t pde = pd[pd_idx];
    uint64_t* pt;
    if (!(pde & PAGE_PRESENT)) {
        pt = (uint64_t*)alloc_page();
        if (!pt) return;
        memset_asm(pt, 0, 4096);
        pd[pd_idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER ? PAGE_USER : 0);
    } else {
        pt = (uint64_t*)(pde & ~0xFFF);
    }

    pt[pt_idx] = (uint64_t)phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & ~0xFFF);
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void map_page(void* phys, void* virt, uint32_t flags) {
    map_pml4(kernel_pml4, phys, virt, flags);
}

void map_page_dir(uint64_t* pml4, void* phys, void* virt, uint32_t flags) {
    map_pml4(pml4, phys, virt, flags);
}

void unmap_page(void* virt) {
    uint64_t vaddr = (uint64_t)virt;
    int pml4_idx = (vaddr >> 39) & 0x1FF;
    int pdpt_idx = (vaddr >> 30) & 0x1FF;
    int pd_idx   = (vaddr >> 21) & 0x1FF;
    int pt_idx   = (vaddr >> 12) & 0x1FF;

    if (!kernel_pml4) return;
    uint64_t pml4e = kernel_pml4[pml4_idx];
    if (!(pml4e & PAGE_PRESENT)) return;

    uint64_t* pdpt = (uint64_t*)(pml4e & ~0xFFF);
    uint64_t pdpte = pdpt[pdpt_idx];
    if (!(pdpte & PAGE_PRESENT)) return;

    uint64_t* pd = (uint64_t*)(pdpte & ~0xFFF);
    uint64_t pde = pd[pd_idx];
    if (!(pde & PAGE_PRESENT)) return;

    uint64_t* pt = (uint64_t*)(pde & ~0xFFF);
    pt[pt_idx] = 0;
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

uint64_t* get_kernel_page_directory(void) {
    return kernel_pml4;
}

// Physical address of kernel PML4 for assembly page-table switching
uint64_t kernel_pml4_phys = 0;

void switch_page_directory(uint64_t* pml4) {
    if (!pml4) pml4 = kernel_pml4;
    current_pml4 = pml4;
    write_cr3((uint64_t)pml4);
}

uint64_t* alloc_page_directory(void) {
    uint64_t* pml4 = (uint64_t*)alloc_page();
    if (!pml4) return NULL;
    memset_asm(pml4, 0, PAGE_SIZE);

    // Clone kernel PML4 entries for the higher half only (PML4[256..511])
    // Identity mapping (PML4[0]) is NOT shared — user processes use higher half
    for (int i = PML4_HIGHER; i < 512; i++) {
        if (kernel_pml4[i] & PAGE_PRESENT) {
            pml4[i] = kernel_pml4[i];
        }
    }
    return pml4;
}

void* clone_page_directory(void) {
    return (void*)alloc_page_directory();
}

// Identity map using 2MB huge pages for speed
void init_paging(void) {
    printf("[PAGING] Allocating PML4 table...\n");
    kernel_pml4 = (uint64_t*)alloc_page();
    if (!kernel_pml4) kernel_panic("No PML4 page");
    memset_asm(kernel_pml4, 0, PAGE_SIZE);

    printf("[PAGING] Identity-mapping %d MB...\n", IDENTITY_MAP_MB);
    // Allocate one PDP table (covers up to 512 GB)
    uint64_t* pdpt = (uint64_t*)alloc_page();
    if (!pdpt) kernel_panic("No PDPT page");
    memset_asm(pdpt, 0, PAGE_SIZE);

    // Allocate one Page Directory with 2MB huge-page entries
    uint64_t* pd = (uint64_t*)alloc_page();
    if (!pd) kernel_panic("No PD page");
    memset_asm(pd, 0, PAGE_SIZE);

    int num_pages = IDENTITY_MAP_MB / 2; // 2MB per entry
    for (int i = 0; i < num_pages; i++) {
        pd[i] = ((uint64_t)i * 0x200000) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    }

    pdpt[0] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITABLE;
    kernel_pml4[PML4_IDENTITY] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE;

    // Mirror identity mapping in higher half (PML4[256] = PML4[0])
    kernel_pml4[PML4_HIGHER] = kernel_pml4[PML4_IDENTITY];

    // Expose physical address for assembly page-table switching
    kernel_pml4_phys = (uint64_t)kernel_pml4;

    printf("[PAGING] Loading CR3 with %lx\n", (uint64_t)kernel_pml4);
    write_cr3((uint64_t)kernel_pml4);

    printf("[PAGING] Enabled successfully.\n");
    current_pml4 = kernel_pml4;
}
