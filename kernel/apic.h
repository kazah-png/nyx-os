#ifndef APIC_H
#define APIC_H

// IA32_APIC_BASE MSR (0x1B)
#define IA32_APIC_BASE_MSR  0x1B
#define APIC_BASE_BSP     (1 << 8)
#define APIC_BASE_ENABLE  (1 << 11)
#define APIC_BASE_ADDR_MASK (~0xFFFULL)

// Local APIC registers (offset from MMIO base 0xFEE00000)
#define LAPIC_ID      0x020
#define LAPIC_VERSION 0x030
#define LAPIC_TPR     0x080
#define LAPIC_APR     0x090
#define LAPIC_PPR     0x0A0
#define LAPIC_EOI     0x0B0
#define LAPIC_LDR     0x0D0
#define LAPIC_DFR     0x0E0
#define LAPIC_SVR     0x0F0
#define LAPIC_ISR0    0x100
#define LAPIC_TMR0    0x180
#define LAPIC_IRR0    0x200
#define LAPIC_ESR     0x280
#define LAPIC_ICR0    0x300
#define LAPIC_ICR1    0x310
#define LAPIC_LVT_TIMER   0x320
#define LAPIC_LVT_THERMAL 0x330
#define LAPIC_LVT_PERFMON 0x340
#define LAPIC_LVT_LINT0   0x350
#define LAPIC_LVT_LINT1   0x360
#define LAPIC_LVT_ERROR   0x370
#define LAPIC_TIMER_INITCNT  0x380
#define LAPIC_TIMER_CURCNT   0x390
#define LAPIC_TIMER_DIV      0x3E0

// LAPIC_TIMER_DIV encoding (bit 2 is skipped by the architecture)
#define LAPIC_TIMER_DIV16    0x3

// LVT entry flags
#define LVT_MASKED    (1 << 16)
#define LVT_TIMER_PERIODIC (1 << 17)   // LVT_TIMER mode: reload from INITCNT forever
#define LVT_TRIGGER   (1 << 15)
#define LVT_LEVEL     (1 << 14)
#define LVT_FIXED     0
#define LVT_NMI       0x400
#define LVT_EXTINT    0x700

// SVR flags
#define SVR_ENABLE    (1 << 8)
#define SVR_FOCUS     (1 << 9)

// Local APIC default base
#define LAPIC_BASE    0xFEE00000

// I/O APIC registers
#define IOAPIC_IOREGSEL 0x00
#define IOAPIC_IOWIN    0x10
#define IOAPIC_ID      0x00
#define IOAPIC_VERSION 0x01
#define IOAPIC_ARB     0x02
#define IOAPIC_REDTBL  0x10

// I/O APIC redirection entry flags
#define IOAPIC_INT_MASKED    (1 << 16)
#define IOAPIC_INT_PHYSICAL  0
#define IOAPIC_INT_LOGICAL   (1 << 11)
#define IOAPIC_INT_EDGE      0
#define IOAPIC_INT_LEVEL     (1 << 15)
#define IOAPIC_INT_ACTIVE_HI 0
#define IOAPIC_INT_ACTIVE_LO (1 << 13)

// Default I/O APIC base (QEMU)
#define IOAPIC_BASE   0xFEC00000

// ICR delivery modes
#define ICR_INIT         (5 << 8)
#define ICR_STARTUP      (6 << 8)
#define ICR_PHYSICAL     0
#define ICR_LOGICAL      (1 << 11)
#define ICR_ASSERT       (1 << 14)
#define ICR_EDGE         0
#define ICR_LEVEL        (1 << 15)
#define ICR_NO_SHOOTDOWN 0
#define ICR_FIXED        0

void init_apic(void);
void apic_eoi(void);
void ioapic_redirect_irq(uint8_t irq, uint8_t vector, uint8_t apic_id);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
uint32_t apic_get_id(void);
uint32_t apic_get_cpu_count(void);
void apic_send_ipi(uint32_t apic_id, uint32_t icr_low);
void apic_send_init_ipi(uint32_t apic_id);
void apic_send_sipi(uint32_t apic_id, uint8_t vector);
uint32_t apic_wait_delivery(void);
extern int apic_initialized;
extern volatile uint32_t* lapic;

#endif
