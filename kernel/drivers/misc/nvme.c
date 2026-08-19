#include "../../core/kernel.h"
#include "../../mm/paging.h"
#include "pci.h"
#include "nvme.h"

// --- MMIO accessors (BAR0 is identity-mapped uncached by map_mmio) ------------
static inline uint32_t mmio_r32(uint64_t base, uint32_t off) { return *(volatile uint32_t*)(base + off); }
static inline void     mmio_w32(uint64_t base, uint32_t off, uint32_t v) { *(volatile uint32_t*)(base + off) = v; }
static inline uint64_t mmio_r64(uint64_t base, uint32_t off) { return *(volatile uint64_t*)(base + off); }
static inline void     mmio_w64(uint64_t base, uint32_t off, uint64_t v) { *(volatile uint64_t*)(base + off) = v; }

// --- pure register math (KAT'd, no hardware) ----------------------------------
// CAP (Controller Capabilities) sub-fields.
static uint32_t cap_mqes(uint64_t cap)   { return (uint32_t)(cap & 0xFFFF); }        // max queue entries (0-based)
static uint32_t cap_to(uint64_t cap)     { return (uint32_t)((cap >> 24) & 0xFF); }  // timeout, 500ms units
static uint32_t cap_dstrd(uint64_t cap)  { return (uint32_t)((cap >> 32) & 0xF); }   // doorbell stride exponent
static uint32_t cap_mpsmin(uint64_t cap) { return (uint32_t)((cap >> 48) & 0xF); }   // min page size (2^(12+x))
// Doorbell register offsets: SQ tail / CQ head for queue `qid`, stride = 4<<dstrd.
static uint32_t sq_db_off(uint32_t qid, uint32_t dstrd) { return NVME_REG_DBS + (2u * qid)      * (4u << dstrd); }
static uint32_t cq_db_off(uint32_t qid, uint32_t dstrd) { return NVME_REG_DBS + (2u * qid + 1u) * (4u << dstrd); }

#define NVME_ASQ_DEPTH 16
#define NVME_ACQ_DEPTH 16

typedef struct {
    int      present;
    uint64_t mmio;                 // BAR0 base (identity-mapped)
    uint32_t dstrd;                // doorbell stride exponent from CAP
    uint32_t mqes;                 // max queue entries supported
    uint64_t asq_phys, acq_phys;   // admin queue backing pages
    uint16_t vendor, device;
} nvme_dev_t;
static nvme_dev_t nvme_dev = {0};

// Spin until CSTS.RDY matches `want` (1=ready, 0=idle), or the controller faults
// (CSTS.CFS) / we time out. Bounded busy-loop — no dependency on the timer.
static int wait_ready(uint64_t base, int want) {
    for (uint32_t i = 0; i < 20000000u; i++) {
        uint32_t csts = mmio_r32(base, NVME_REG_CSTS);
        if (csts & 0x2) return -1;                       // CFS: controller fatal status
        if ((int)(csts & 0x1) == want) return 0;
        for (volatile int d = 0; d < 20; d++) { }
    }
    return -1;
}

int nvme_init(void) {
    if (nvme_dev.present) {
        printf("nvme: controller already up (%04x:%04x, ASQ depth %u)\n",
               nvme_dev.vendor, nvme_dev.device, NVME_ASQ_DEPTH);
        return 0;
    }

    // 1. Discover the controller (PCI class 01h subclass 08h) via the lspci scan.
    static pci_dev_t devs[96];
    int n = pci_enumerate(devs, 96);
    pci_dev_t* c = 0;
    for (int i = 0; i < n; i++)
        if (devs[i].class_code == 0x01 && devs[i].subclass == 0x08) { c = &devs[i]; break; }
    if (!c) { printf("nvme: no NVMe controller found (need PCI class 01:08 - see lspci)\n"); return -1; }

    // 2. BAR0 -> MMIO base (handle a 64-bit memory BAR: high dword lives in BAR1).
    uint32_t bar0 = pci_cfg_read32(c->bus, c->slot, c->func, 0x10);
    uint64_t base = (uint64_t)(bar0 & ~0xFu);
    if ((bar0 & 0x6) == 0x4) {
        uint32_t bar1 = pci_cfg_read32(c->bus, c->slot, c->func, 0x14);
        base |= ((uint64_t)bar1) << 32;
    }
    if (!base) { printf("nvme: controller BAR0 not assigned\n"); return -1; }

    // 3. Enable PCI memory space + bus mastering (the admin queue is DMA).
    uint32_t cmd = pci_cfg_read32(c->bus, c->slot, c->func, 0x04);
    pci_cfg_write32(c->bus, c->slot, c->func, 0x04, cmd | 0x6);

    // 4. Map the register window (regs + admin doorbells) uncached.
    map_mmio_range(base, 0x2000);

    uint64_t cap = mmio_r64(base, NVME_REG_CAP);
    nvme_dev.mmio   = base;
    nvme_dev.dstrd  = cap_dstrd(cap);
    nvme_dev.mqes   = cap_mqes(cap);
    nvme_dev.vendor = c->vendor;
    nvme_dev.device = c->device;
    uint32_t vs = mmio_r32(base, NVME_REG_VS);
    printf("nvme: %04x:%04x MMIO=%08x%08x VS=%u.%u CAP MQES=%u DSTRD=%u TO=%u MPSMIN=%u\n",
           c->vendor, c->device, (uint32_t)(base >> 32), (uint32_t)base,
           (vs >> 16) & 0xFFFF, (vs >> 8) & 0xFF,
           cap_mqes(cap), cap_dstrd(cap), cap_to(cap), cap_mpsmin(cap));

    // 5. Reset: clear CC.EN, wait for CSTS.RDY=0.
    mmio_w32(base, NVME_REG_CC, 0);
    if (wait_ready(base, 0) < 0) { printf("nvme: controller would not go idle (CSTS.RDY!=0)\n"); return -1; }

    // 6. Admin queues: one identity-mapped, zeroed page each (their pointer IS the
    //    physical address the controller DMAs against, since low RAM is identity-mapped).
    void* asq = alloc_page();
    void* acq = alloc_page();
    if (!asq || !acq) { printf("nvme: admin-queue page alloc failed\n"); return -1; }
    __builtin_memset(asq, 0, 4096);
    __builtin_memset(acq, 0, 4096);
    nvme_dev.asq_phys = (uint64_t)asq;
    nvme_dev.acq_phys = (uint64_t)acq;

    mmio_w32(base, NVME_REG_AQA, ((NVME_ACQ_DEPTH - 1) << 16) | (NVME_ASQ_DEPTH - 1));
    mmio_w64(base, NVME_REG_ASQ, nvme_dev.asq_phys);
    mmio_w64(base, NVME_REG_ACQ, nvme_dev.acq_phys);

    // 7. Enable: IOCQES=4 (16-byte CQ entries), IOSQES=6 (64-byte SQ entries),
    //    MPS=0 (4 KB), CSS=0 (NVM command set), EN=1. Wait for CSTS.RDY=1.
    uint32_t cc = (4u << 20) | (6u << 16) | (0u << 7) | (0u << 4) | 1u;
    mmio_w32(base, NVME_REG_CC, cc);
    if (wait_ready(base, 1) < 0) { printf("nvme: controller would not become ready (CSTS.RDY!=1)\n"); return -1; }

    nvme_dev.present = 1;
    printf("nvme: controller ready - admin queue up (ASQ@%08x ACQ@%08x depth %u, SQ0 db off 0x%x)\n",
           (uint32_t)nvme_dev.asq_phys, (uint32_t)nvme_dev.acq_phys, NVME_ASQ_DEPTH,
           sq_db_off(0, nvme_dev.dstrd));
    return 0;
}

int nvme_present(void) { return nvme_dev.present; }

// KAT: exercise the pure register math against hand-computed vectors. The MMIO
// path itself is verified live against QEMU's `-device nvme`.
int nvme_selftest(void) {
    uint64_t cap = 0x0001000220001234ULL;      // MQES=0x1234 TO=0x20 DSTRD=2 MPSMIN=1
    if (cap_mqes(cap)   != 0x1234) return 1;
    if (cap_to(cap)     != 0x20)   return 2;
    if (cap_dstrd(cap)  != 2)      return 3;
    if (cap_mpsmin(cap) != 1)      return 4;
    // doorbell stride = 4 << dstrd
    if (sq_db_off(0, 0) != 0x1000) return 5;   // stride 4
    if (cq_db_off(0, 0) != 0x1004) return 6;
    if (sq_db_off(1, 0) != 0x1008) return 7;
    if (cq_db_off(1, 0) != 0x100C) return 8;
    if (sq_db_off(0, 2) != 0x1000) return 9;   // stride 16
    if (cq_db_off(0, 2) != 0x1010) return 10;
    if (sq_db_off(1, 2) != 0x1020) return 11;
    if (cq_db_off(1, 2) != 0x1030) return 12;
    return 0;
}
