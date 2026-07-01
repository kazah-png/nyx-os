#include "kernel.h"
#include "apic.h"

volatile uint32_t* lapic = NULL;
static volatile uint32_t* ioapic = NULL;
static uint32_t ioapic_nr_irqs = 24;
int apic_initialized = 0;

// Read from a Local APIC register
static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)((uintptr_t)lapic + reg);
}

// Write to a Local APIC register
static inline void lapic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)((uintptr_t)lapic + reg) = val;
}

// Read from an I/O APIC register
static uint32_t ioapic_read(uint32_t reg) {
    *(volatile uint32_t*)ioapic = reg;
    return *(volatile uint32_t*)((uintptr_t)ioapic + IOAPIC_IOWIN);
}

// Write to an I/O APIC register
static void ioapic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)ioapic = reg;
    *(volatile uint32_t*)((uintptr_t)ioapic + IOAPIC_IOWIN) = val;
}

// Map a physical page for MMIO access
static void map_mmio(uint64_t phys) {
    map_page((void*)phys, (void*)phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_NX);
}

// Detect CPU features using CPUID
static int cpuid_apic_detected(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx & (1 << 9)) != 0; // CPUID.01h:EDX.APIC bit
}

static int cpuid_x2apic_detected(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (ecx & (1 << 21)) != 0; // CPUID.01h:ECX.x2APIC bit
}

// Write 0 to the PIC to mask everything
static void mask_legacy_pic(void) {
    outb(0xA1, 0xFF); // Slave PIC
    outb(0x21, 0xFF); // Master PIC
}

void init_apic(void) {
    printf("[APIC] Initializing...\n");

    // Check CPU support
    if (!cpuid_apic_detected()) {
        printf("[APIC] Local APIC not supported by CPU, skipping.\n");
        return;
    }

    // Map Local APIC MMIO region
    map_mmio(LAPIC_BASE);
    lapic = (volatile uint32_t*)LAPIC_BASE;

    // Enable Local APIC via MSR
    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    if (!(apic_base & APIC_BASE_ENABLE)) {
        printf("[APIC] Enabling Local APIC via MSR...\n");
        write_msr(IA32_APIC_BASE_MSR, apic_base | APIC_BASE_ENABLE);
    }

    uint32_t apic_version = lapic_read(LAPIC_VERSION);
    printf("[APIC] Version: 0x%x, Max LVT: %d\n",
           apic_version & 0xFF, (apic_version >> 16) & 0xFF);

    uint32_t apic_id = lapic_read(LAPIC_ID) >> 24;
    printf("[APIC] BSP APIC ID: %d\n", apic_id);

    // Software enable Local APIC via SVR
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr |= SVR_ENABLE;
    svr &= ~SVR_FOCUS; // Disable focus processor
    svr = (svr & ~0xFF) | 0x0F; // Spurious vector = 0x0F (unused)
    lapic_write(LAPIC_SVR, svr);

    // Mask all LVT entries
    lapic_write(LAPIC_LVT_TIMER,   LVT_MASKED);
    lapic_write(LAPIC_LVT_THERMAL, LVT_MASKED);
    lapic_write(LAPIC_LVT_PERFMON, LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0,   LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1,   LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR,   LVT_MASKED);

    // Set Task Priority to 0 (accept all interrupts)
    lapic_write(LAPIC_TPR, 0);

    printf("[APIC] Local APIC initialized.\n");

    // Map I/O APIC MMIO region
    map_mmio(IOAPIC_BASE);
    ioapic = (volatile uint32_t*)IOAPIC_BASE;

    // Read I/O APIC version and max entries
    uint32_t ioapic_ver = ioapic_read(IOAPIC_VERSION);
    ioapic_nr_irqs = ((ioapic_ver >> 16) & 0xFF) + 1;
    uint32_t ioapic_id = ioapic_read(IOAPIC_ID) >> 24;

    printf("[APIC] I/O APIC ID: %d, Version: 0x%x, Max IRQs: %d\n",
           ioapic_id, ioapic_ver & 0xFF, ioapic_nr_irqs);

    // Mask all I/O APIC redirection entries
    for (int i = 0; i < ioapic_nr_irqs; i++) {
        uint32_t reg = IOAPIC_REDTBL + i * 2;
        uint64_t entry = IOAPIC_INT_MASKED;
        ioapic_write(reg, (uint32_t)entry);
        ioapic_write(reg + 1, (uint32_t)(entry >> 32));
    }

    printf("[APIC] I/O APIC initialized.\n");

    // Route legacy IRQs through I/O APIC (all masked, unmask individually)
    for (int i = 0; i < 16; i++) {
        ioapic_redirect_irq(i, 32 + i, apic_id);
        ioapic_mask_irq(i);
    }

    // Mask the legacy 8259 PIC
    mask_legacy_pic();

    printf("[APIC] Legacy PIC disabled, IRQs routed through I/O APIC.\n");

    apic_initialized = 1;
}

void apic_eoi(void) {
    if (lapic) {
        lapic_write(LAPIC_EOI, 0);
    }
}

void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint8_t apic_id) {
    if (!ioapic || irq >= ioapic_nr_irqs) return;
    uint32_t reg = IOAPIC_REDTBL + irq * 2;

    // Build 64-bit redirection entry
    // Edge-triggered, active high, fixed delivery, physical mode
    uint64_t entry = vector;
    entry |= (uint64_t)apic_id << 56; // Destination APIC ID

    ioapic_write(reg, (uint32_t)entry);
    ioapic_write(reg + 1, (uint32_t)(entry >> 32));
}

void ioapic_mask_irq(uint8_t irq) {
    if (!ioapic || irq >= ioapic_nr_irqs) return;
    uint32_t reg = IOAPIC_REDTBL + irq * 2;
    uint32_t low = ioapic_read(reg);
    ioapic_write(reg, low | IOAPIC_INT_MASKED);
}

void ioapic_unmask_irq(uint8_t irq) {
    if (!ioapic || irq >= ioapic_nr_irqs) return;
    uint32_t reg = IOAPIC_REDTBL + irq * 2;
    uint32_t low = ioapic_read(reg);
    ioapic_write(reg, low & ~IOAPIC_INT_MASKED);
}

uint32_t apic_get_id(void) {
    if (!lapic) return 0;
    return lapic_read(LAPIC_ID) >> 24;
}

uint32_t apic_get_cpu_count(void) {
    return cpu_count;
}

uint32_t apic_wait_delivery(void) {
    if (!lapic) return 1;
    int timeout = 1000000;
    while (lapic_read(LAPIC_ICR0) & (1 << 12)) {
        __asm__ volatile("pause");
        if (--timeout == 0) return 1;
    }
    return 0;
}

void apic_send_ipi(uint32_t apic_id, uint32_t icr_low) {
    if (!lapic) return;
    if (apic_wait_delivery()) {
        printf("[APIC] WARNING: ICR delivery timeout (stuck busy?)\n");
    }
    lapic_write(LAPIC_ICR1, apic_id << 24);
    __asm__ volatile("mfence");
    lapic_write(LAPIC_ICR0, icr_low);
}

void apic_send_init_ipi(uint32_t apic_id) {
    // Level-assert INIT
    apic_send_ipi(apic_id, ICR_INIT | ICR_PHYSICAL | ICR_LEVEL | ICR_ASSERT);
    // Small delay
    for (volatile int d = 0; d < 10000; d++) __asm__ volatile("nop");
    // Level-de-assert to complete the level-triggered message
    apic_send_ipi(apic_id, ICR_INIT | ICR_PHYSICAL | ICR_LEVEL);
}

void apic_send_sipi(uint32_t apic_id, uint8_t vector) {
    apic_send_ipi(apic_id, ICR_STARTUP | ICR_PHYSICAL | (vector & 0xFF));
}
