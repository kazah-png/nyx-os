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
    uint32_t to;                   // CAP.TO: max ready timeout, 500ms units
    uint64_t asq_phys, acq_phys;   // admin queue backing pages
    uint16_t vendor, device;
    // admin queue software state (single command in flight at a time)
    uint32_t sq_tail, cq_head, cq_phase;
    // filled by nvme_identify()
    int      identified;
    uint64_t nsze;                 // namespace size in logical blocks
    uint32_t lba_size;             // bytes per logical block
    char     model[41], serial[21];
    // I/O queue pair (qid 1)
    int      io_ready;
    uint64_t io_sq_phys, io_cq_phys;
    uint32_t io_sq_tail, io_cq_head, io_cq_phase;
} nvme_dev_t;

#define NVME_IOQ_DEPTH 8               // I/O queue entries (SQ 64B/entry, CQ 16B/entry; fits one page)
static nvme_dev_t nvme_dev = {0};

// Little-endian scalar reads out of a DMA'd identify buffer.
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

// Build a 64-byte admin IDENTIFY SQE (opcode 0x06) into `sqe` (16 dwords). Pure
// (KAT'd): CDW0 opcode+CID, NSID, PRP1 (64-bit data buffer), CDW10 CNS.
static void build_identify(uint32_t* sqe, uint32_t nsid, uint32_t cns, uint64_t prp1, uint16_t cid) {
    for (int i = 0; i < 16; i++) sqe[i] = 0;
    sqe[0]  = 0x06u | ((uint32_t)cid << 16);   // CDW0: opcode 0x06 (IDENTIFY), CID
    sqe[1]  = nsid;                            // NSID (1 for namespace, 0 for controller)
    sqe[6]  = (uint32_t)prp1;                  // PRP1 low
    sqe[7]  = (uint32_t)(prp1 >> 32);          // PRP1 high (data fits one 4 KB page -> PRP2=0)
    sqe[10] = cns;                             // CDW10: CNS (0=namespace, 1=controller)
}

// Parse the IDENTIFY-NAMESPACE structure (pure, KAT'd): total blocks (NSZE) and
// bytes-per-block from the formatted LBA format (FLBAS -> LBAF[idx].LBADS).
static uint64_t ns_nsze(const uint8_t* d) { return rd64(d + 0); }
static uint32_t ns_lba_size(const uint8_t* d) {
    uint32_t idx   = (uint32_t)d[26] & 0xF;                 // FLBAS: selected LBA format
    uint32_t lbaf  = rd32(d + 128 + idx * 4);
    uint32_t lbads = (lbaf >> 16) & 0xFF;                   // LBA data size exponent
    return (lbads < 32) ? (1u << lbads) : 0;
}

// SET FEATURES (opcode 0x09) FID 0x07 = Number of Queues: request nsq+1 IO
// submission and ncq+1 completion queues (the fields are 0-based). Pure/KAT'd.
static void build_set_nqueues(uint32_t* sqe, uint32_t nsq0, uint32_t ncq0, uint16_t cid) {
    for (int i = 0; i < 16; i++) sqe[i] = 0;
    sqe[0]  = 0x09u | ((uint32_t)cid << 16);   // CDW0: SET FEATURES
    sqe[10] = 0x07u;                           // CDW10: FID = Number of Queues
    sqe[11] = (ncq0 << 16) | (nsq0 & 0xFFFF);  // CDW11: NCQR:NSQR (0-based)
}
// CREATE I/O COMPLETION QUEUE (opcode 0x05): PRP1=CQ page, CDW10=(qsize0<<16)|qid,
// CDW11 PC=1 (contiguous), IEN=0 (polled). Pure/KAT'd.
static void build_create_io_cq(uint32_t* sqe, uint64_t prp1, uint16_t qid, uint16_t qsize0, uint16_t cid) {
    for (int i = 0; i < 16; i++) sqe[i] = 0;
    sqe[0]  = 0x05u | ((uint32_t)cid << 16);
    sqe[6]  = (uint32_t)prp1;
    sqe[7]  = (uint32_t)(prp1 >> 32);
    sqe[10] = ((uint32_t)qsize0 << 16) | qid;
    sqe[11] = 0x1u;                             // PC=1, IEN=0
}
// CREATE I/O SUBMISSION QUEUE (opcode 0x01): PRP1=SQ page, CDW10=(qsize0<<16)|qid,
// CDW11=(cqid<<16)|PC. Pure/KAT'd.
static void build_create_io_sq(uint32_t* sqe, uint64_t prp1, uint16_t qid, uint16_t qsize0, uint16_t cqid, uint16_t cid) {
    for (int i = 0; i < 16; i++) sqe[i] = 0;
    sqe[0]  = 0x01u | ((uint32_t)cid << 16);
    sqe[6]  = (uint32_t)prp1;
    sqe[7]  = (uint32_t)(prp1 >> 32);
    sqe[10] = ((uint32_t)qsize0 << 16) | qid;
    sqe[11] = ((uint32_t)cqid << 16) | 0x1u;   // CQID, PC=1
}
// NVM READ (opcode 0x02) / WRITE (0x01) for one contiguous run: NSID, PRP1=first
// data page, PRP2 (0, the 2nd page, or a PRP-list page — see nvme_build_prps),
// SLBA (64-bit split across CDW10/11), NLB (0-based) in CDW12. Pure/KAT'd.
static void build_io_rw(uint32_t* sqe, int write, uint32_t nsid, uint64_t slba, uint64_t prp1, uint64_t prp2, uint16_t nlb0, uint16_t cid) {
    for (int i = 0; i < 16; i++) sqe[i] = 0;
    sqe[0]  = (write ? 0x01u : 0x02u) | ((uint32_t)cid << 16);
    sqe[1]  = nsid;
    sqe[6]  = (uint32_t)prp1;                  // PRP1 low
    sqe[7]  = (uint32_t)(prp1 >> 32);          // PRP1 high
    sqe[8]  = (uint32_t)prp2;                  // PRP2 low  (2nd page or PRP-list)
    sqe[9]  = (uint32_t)(prp2 >> 32);          // PRP2 high
    sqe[10] = (uint32_t)slba;                  // SLBA low
    sqe[11] = (uint32_t)(slba >> 32);          // SLBA high
    sqe[12] = nlb0;                            // NLB (0-based; 0 = one block)
}

// Pure (KAT'd): given the DMA buffer physical base (page-aligned), block count and
// block size, compute PRP1/PRP2 for one NVM command. PRP1 is the first page; PRP2
// is 0 (<=1 page), the second page (exactly 2 pages), or a pointer to `list` (a
// PRP-list page holding the 2nd..last page addresses) for >2 pages. Because
// buf_phys is page-aligned every page pointer has offset 0. Returns the page span.
static uint32_t nvme_build_prps(uint64_t buf_phys, uint32_t nblocks, uint32_t bs,
                                uint64_t* list, uint64_t* prp1, uint64_t* prp2) {
    uint64_t bytes  = (uint64_t)nblocks * bs;
    uint32_t npages = (uint32_t)((bytes + 4095) / 4096);
    if (npages == 0) npages = 1;
    *prp1 = buf_phys;
    if (npages <= 1) {
        *prp2 = 0;
    } else if (npages == 2) {
        *prp2 = buf_phys + 4096;                       // PRP2 = 2nd page directly
    } else {
        for (uint32_t i = 0; i + 1 < npages; i++)      // list[0..npages-2] = pages 1..npages-1
            list[i] = buf_phys + (uint64_t)(i + 1) * 4096;
        *prp2 = (uint64_t)list;
    }
    return npages;
}

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
    nvme_dev.to     = cap_to(cap);
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

    // Admin queue starts empty; the controller writes new CQ entries with phase=1
    // on the first pass through the zeroed queue.
    nvme_dev.sq_tail = 0; nvme_dev.cq_head = 0; nvme_dev.cq_phase = 1;
    nvme_dev.present = 1;
    printf("nvme: controller ready - admin queue up (ASQ@%08x ACQ@%08x depth %u, SQ0 db off 0x%x)\n",
           (uint32_t)nvme_dev.asq_phys, (uint32_t)nvme_dev.acq_phys, NVME_ASQ_DEPTH,
           sq_db_off(0, nvme_dev.dstrd));
    return 0;
}

// Submit one already-built 64-byte admin command, ring the SQ tail doorbell, and
// poll the admin CQ for its completion (phase-bit flip). Advances the queue
// pointers + rings the CQ head doorbell. Returns the NVMe status (0 = success),
// or -1 on timeout. Timeout scales to CAP.TO (500ms units) so a slow real SSD
// (the target laptop reported TO=240 = 120s) is tolerated.
static int nvme_admin_submit(const uint32_t* sqe) {
    uint64_t base = nvme_dev.mmio;
    volatile uint32_t* asq = (volatile uint32_t*)nvme_dev.asq_phys;
    volatile uint32_t* acq = (volatile uint32_t*)nvme_dev.acq_phys;

    volatile uint32_t* slot = &asq[nvme_dev.sq_tail * 16];
    for (int i = 0; i < 16; i++) slot[i] = sqe[i];
    nvme_dev.sq_tail = (nvme_dev.sq_tail + 1) % NVME_ASQ_DEPTH;
    mmio_w32(base, sq_db_off(0, nvme_dev.dstrd), nvme_dev.sq_tail);

    volatile uint32_t* cqe = &acq[nvme_dev.cq_head * 4];
    uint64_t budget = (uint64_t)(nvme_dev.to + 1) * 2000000ULL;   // ~ CAP.TO-scaled spin
    for (uint64_t i = 0; i < budget; i++) {
        uint32_t d3 = cqe[3];
        if (((d3 >> 16) & 1u) == nvme_dev.cq_phase) {
            uint32_t status = (d3 >> 17) & 0x7FFu;
            nvme_dev.cq_head = (nvme_dev.cq_head + 1) % NVME_ACQ_DEPTH;
            if (nvme_dev.cq_head == 0) nvme_dev.cq_phase ^= 1u;
            mmio_w32(base, cq_db_off(0, nvme_dev.dstrd), nvme_dev.cq_head);
            return (int)status;
        }
        for (volatile int d = 0; d < 8; d++) { }
    }
    return -1;
}

// Copy a fixed-width, space-padded ASCII field (NVMe model/serial strings are not
// NUL-terminated), trimming trailing spaces.
static void copy_ident_str(char* dst, const uint8_t* src, int n) {
    int end = n;
    while (end > 0 && (src[end - 1] == ' ' || src[end - 1] == 0)) end--;
    int i = 0;
    for (; i < end && i < n; i++) dst[i] = (char)src[i];
    dst[i] = 0;
}

// Rung 2: IDENTIFY the controller (model/serial) and namespace 1 (capacity + LBA
// size) via two admin commands into a shared DMA page. Fills nvme_dev + prints.
int nvme_identify(void) {
    if (!nvme_dev.present) { printf("nvme: controller not up (run nvme first)\n"); return -1; }
    if (nvme_dev.identified) return 0;              // already done — don't re-submit / re-alloc

    void* buf = alloc_page();                       // 4 KB identity page = the PRP1 target
    if (!buf) { printf("nvme: identify buffer alloc failed\n"); return -1; }
    uint8_t* d = (uint8_t*)buf;

    // IDENTIFY controller (CNS=1, NSID=0)
    uint32_t sqe[16];
    __builtin_memset(buf, 0, 4096);
    build_identify(sqe, 0, 1, (uint64_t)buf, 0x01);
    int st = nvme_admin_submit(sqe);
    if (st != 0) { printf("nvme: IDENTIFY controller failed (status=%d)\n", st); return -1; }
    copy_ident_str(nvme_dev.serial, d + 4, 20);
    copy_ident_str(nvme_dev.model,  d + 24, 40);

    // IDENTIFY namespace 1 (CNS=0, NSID=1)
    __builtin_memset(buf, 0, 4096);
    build_identify(sqe, 1, 0, (uint64_t)buf, 0x02);
    st = nvme_admin_submit(sqe);
    if (st != 0) { printf("nvme: IDENTIFY namespace failed (status=%d)\n", st); return -1; }
    nvme_dev.nsze     = ns_nsze(d);
    nvme_dev.lba_size = ns_lba_size(d);
    nvme_dev.identified = 1;

    // capacity = blocks * bytes-per-block, reported in MiB (kept in a u64)
    uint64_t cap_bytes = nvme_dev.nsze * (uint64_t)nvme_dev.lba_size;
    uint32_t cap_mib   = (uint32_t)(cap_bytes >> 20);
    printf("nvme: model '%s' serial '%s'\n", nvme_dev.model, nvme_dev.serial);
    printf("nvme: namespace 1: %u blocks x %u B = %u MiB\n",
           (uint32_t)nvme_dev.nsze, nvme_dev.lba_size, cap_mib);
    return 0;
}

// Rung 3: create one I/O SQ/CQ queue pair (qid 1) via admin commands — SET
// FEATURES (number of queues) -> CREATE IO CQ -> CREATE IO SQ. The queue pages are
// identity-mapped alloc_page()s (their pointer IS the DMA physical address).
int nvme_create_io_queues(void) {
    if (!nvme_dev.present) { printf("nvme: controller not up (run nvme first)\n"); return -1; }
    if (nvme_dev.io_ready) { printf("nvme: I/O queue pair already up (qid 1, depth %u)\n", NVME_IOQ_DEPTH); return 0; }

    uint32_t sqe[16];
    build_set_nqueues(sqe, 0, 0, 0x10);                     // request 1 IO SQ + 1 IO CQ (0-based)
    int st = nvme_admin_submit(sqe);
    if (st != 0) { printf("nvme: SET FEATURES (num queues) failed (status=%d)\n", st); return -1; }

    void* cq = alloc_page();
    void* sq = alloc_page();
    if (!cq || !sq) { printf("nvme: I/O queue page alloc failed\n"); return -1; }
    __builtin_memset(cq, 0, 4096);
    __builtin_memset(sq, 0, 4096);
    nvme_dev.io_cq_phys = (uint64_t)cq;
    nvme_dev.io_sq_phys = (uint64_t)sq;

    // the CQ must exist before the SQ that targets it
    build_create_io_cq(sqe, nvme_dev.io_cq_phys, 1, NVME_IOQ_DEPTH - 1, 0x11);
    st = nvme_admin_submit(sqe);
    if (st != 0) { printf("nvme: CREATE IO CQ failed (status=%d)\n", st); return -1; }

    build_create_io_sq(sqe, nvme_dev.io_sq_phys, 1, NVME_IOQ_DEPTH - 1, 1, 0x12);
    st = nvme_admin_submit(sqe);
    if (st != 0) { printf("nvme: CREATE IO SQ failed (status=%d)\n", st); return -1; }

    nvme_dev.io_sq_tail = 0; nvme_dev.io_cq_head = 0; nvme_dev.io_cq_phase = 1;
    nvme_dev.io_ready = 1;
    printf("nvme: I/O queue pair created (qid 1, depth %u, SQ1 db 0x%x CQ1 db 0x%x)\n",
           NVME_IOQ_DEPTH, sq_db_off(1, nvme_dev.dstrd), cq_db_off(1, nvme_dev.dstrd));
    return 0;
}

// Submit one already-built I/O command on the qid-1 SQ, poll the qid-1 CQ. Same
// shape as nvme_admin_submit but on the IO queue + its own doorbells/phase state.
static int nvme_io_submit(const uint32_t* sqe) {
    uint64_t base = nvme_dev.mmio;
    volatile uint32_t* iosq = (volatile uint32_t*)nvme_dev.io_sq_phys;
    volatile uint32_t* iocq = (volatile uint32_t*)nvme_dev.io_cq_phys;

    volatile uint32_t* slot = &iosq[nvme_dev.io_sq_tail * 16];
    for (int i = 0; i < 16; i++) slot[i] = sqe[i];
    nvme_dev.io_sq_tail = (nvme_dev.io_sq_tail + 1) % NVME_IOQ_DEPTH;
    mmio_w32(base, sq_db_off(1, nvme_dev.dstrd), nvme_dev.io_sq_tail);

    volatile uint32_t* cqe = &iocq[nvme_dev.io_cq_head * 4];
    uint64_t budget = (uint64_t)(nvme_dev.to + 1) * 2000000ULL;
    for (uint64_t i = 0; i < budget; i++) {
        uint32_t d3 = cqe[3];
        if (((d3 >> 16) & 1u) == nvme_dev.io_cq_phase) {
            uint32_t status = (d3 >> 17) & 0x7FFu;
            nvme_dev.io_cq_head = (nvme_dev.io_cq_head + 1) % NVME_IOQ_DEPTH;
            if (nvme_dev.io_cq_head == 0) nvme_dev.io_cq_phase ^= 1u;
            mmio_w32(base, cq_db_off(1, nvme_dev.dstrd), nvme_dev.io_cq_head);
            return (int)status;
        }
        for (volatile int d = 0; d < 8; d++) { }
    }
    return -1;
}

// One 4 KB page for the PRP list (holds up to 512 page pointers; we use <=15).
static uint64_t nvme_prp_list[512] __attribute__((aligned(4096)));

// Issue ONE NVM read/write command spanning `nblocks` contiguous blocks from
// `slba`. `buf` MUST be page-aligned and hold nblocks*lba_size bytes; `nblocks` is
// capped by the caller to the transfer buffer's reach (a single command, NLB>0,
// with PRP1 + PRP2 (direct or PRP-list) describing the pages). WRITE mutates the
// medium — callers must never write a real user disk.
int nvme_io_n(int write, uint64_t slba, uint32_t nblocks, void* buf) {
    if (!nvme_dev.io_ready) { printf("nvme: I/O queue not up (run nvme first)\n"); return -1; }
    if (nblocks == 0) return 0;
    uint32_t bs = nvme_dev.lba_size ? nvme_dev.lba_size : 512;
    uint64_t prp1, prp2;
    nvme_build_prps((uint64_t)buf, nblocks, bs, nvme_prp_list, &prp1, &prp2);
    uint32_t sqe[16];
    build_io_rw(sqe, write, 1, slba, prp1, prp2, (uint16_t)(nblocks - 1), (uint16_t)(write ? 0x21 : 0x20));
    int st = nvme_io_submit(sqe);
    if (st != 0) { printf("nvme: I/O %s LBA %u x%u failed (status=%d)\n", write ? "write" : "read", (uint32_t)slba, nblocks, st); return -1; }
    return 0;
}

// Read (write=0) or write (write=1) ONE logical block at `slba` into/from `buf`.
// `buf` MUST be page-aligned. Thin wrapper over the batched path (nblocks=1).
int nvme_io(int write, uint64_t slba, void* buf) {
    return nvme_io_n(write, slba, 1, buf);
}

// READ-ONLY: read the block at `lba` and hex-dump its first 128 bytes. Safe on a
// real disk — it never writes. (`nvme read <lba>`.)
static uint8_t nvme_rbuf[4096] __attribute__((aligned(4096)));
int nvme_dump_lba(uint64_t lba) {
    if (!nvme_dev.io_ready) { printf("nvme: I/O queue not up (run nvme first)\n"); return -1; }
    __builtin_memset(nvme_rbuf, 0, 4096);
    if (nvme_io(0, lba, nvme_rbuf) != 0) return -1;
    printf("nvme: LBA %u (%u B/block) first 128 bytes:\n", (uint32_t)lba, nvme_dev.lba_size);
    for (int r = 0; r < 8; r++) {
        printf("  %04x:", r * 16);
        for (int c = 0; c < 16; c++) printf(" %02x", nvme_rbuf[r * 16 + c]);
        printf("\n");
    }
    return 0;
}

// Multi-block I/O for the installer block layer. Bounces the caller's (arbitrary-
// alignment) buffer through a page-aligned DMA region in <=64 KB commands, issuing
// ONE NVM command per chunk (NLB>0) instead of one per block — a ~64x drop in
// round-trips for the ~4 MB install copy. Returns 0 on success. nvme_write_blocks
// WRITES the medium.
#define NVME_XFER_PAGES 16                                          // 64 KB per command
static uint8_t nvme_xfer[NVME_XFER_PAGES * 4096] __attribute__((aligned(4096)));

int nvme_read_blocks(uint64_t lba, uint32_t count, void* buf) {
    if (!nvme_dev.io_ready) return -1;
    uint32_t bs  = nvme_dev.lba_size ? nvme_dev.lba_size : 512;
    uint32_t per = (NVME_XFER_PAGES * 4096) / bs;                   // blocks per command
    uint8_t* p = (uint8_t*)buf;
    while (count) {
        uint32_t n = count > per ? per : count;
        if (nvme_io_n(0, lba, n, nvme_xfer) != 0) return -1;
        __builtin_memcpy(p, nvme_xfer, (uint64_t)n * bs);
        lba += n; p += (uint64_t)n * bs; count -= n;
    }
    return 0;
}
int nvme_write_blocks(uint64_t lba, uint32_t count, const void* buf) {
    if (!nvme_dev.io_ready) return -1;
    uint32_t bs  = nvme_dev.lba_size ? nvme_dev.lba_size : 512;
    uint32_t per = (NVME_XFER_PAGES * 4096) / bs;
    const uint8_t* p = (const uint8_t*)buf;
    while (count) {
        uint32_t n = count > per ? per : count;
        __builtin_memcpy(nvme_xfer, p, (uint64_t)n * bs);
        if (nvme_io_n(1, lba, n, nvme_xfer) != 0) return -1;
        lba += n; p += (uint64_t)n * bs; count -= n;
    }
    return 0;
}

// Accessors for the installer/`disks` layer (the device struct is file-private).
int         nvme_io_ready(void)        { return nvme_dev.io_ready; }
const char* nvme_model_str(void)       { return nvme_dev.model; }
uint64_t    nvme_capacity_blocks(void) { return nvme_dev.nsze; }
uint32_t    nvme_block_size(void)      { return nvme_dev.lba_size ? nvme_dev.lba_size : 512; }

// QEMU functional check ONLY (needs a real `-device nvme`; NOT in the boot KAT
// battery). Writes a known pattern to a scratch LBA, reads it back, compares.
// WRITES — safe only against an emulated scratch disk, NEVER a real one.
int nvme_io_selftest(void) {
    if (!nvme_dev.io_ready) return -1;
    static uint8_t wbuf[4096] __attribute__((aligned(4096)));
    static uint8_t rbuf[4096] __attribute__((aligned(4096)));
    uint32_t bs = nvme_dev.lba_size ? nvme_dev.lba_size : 512;
    if (bs > 4096) bs = 4096;
    for (uint32_t i = 0; i < bs; i++) wbuf[i] = (uint8_t)(0xA5u ^ (i & 0xFF));
    uint64_t lba = 1000;                                    // scratch LBA on the QEMU image
    if (nvme_io(1, lba, wbuf) != 0) return -2;              // WRITE
    __builtin_memset(rbuf, 0, bs);
    if (nvme_io(0, lba, rbuf) != 0) return -3;              // READ back
    for (uint32_t i = 0; i < bs; i++) if (rbuf[i] != wbuf[i]) return (int)(1000 + i);
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

    // rung 2: IDENTIFY SQE encoding
    uint32_t sqe[16];
    build_identify(sqe, 1, 0, 0x00000001DEADB000ULL, 0x0042);
    if (sqe[0]  != 0x00420006u) return 13;   // CDW0: opcode 0x06 | CID 0x42
    if (sqe[1]  != 1u)          return 14;   // NSID
    if (sqe[6]  != 0xDEADB000u) return 15;   // PRP1 low
    if (sqe[7]  != 0x00000001u) return 16;   // PRP1 high
    if (sqe[10] != 0u)          return 17;   // CNS

    // rung 2: IDENTIFY-NAMESPACE field parse (NSZE + FLBAS -> LBAF -> LBA size)
    static uint8_t ns[256];
    for (int i = 0; i < 256; i++) ns[i] = 0;
    ns[2] = 0x10;                                          // NSZE = 0x100000 blocks (LE)
    ns[26] = 0;                                            // FLBAS -> LBA format 0
    ns[130] = 0x09;                                        // LBAF[0] LBADS=9 -> 512 B
    if (ns_nsze(ns)     != 0x100000ULL) return 18;
    if (ns_lba_size(ns) != 512u)        return 19;
    ns[26] = 1;                                            // FLBAS -> LBA format 1
    ns[134] = 0x0C;                                        // LBAF[1] LBADS=12 -> 4096 B
    if (ns_lba_size(ns) != 4096u)       return 20;

    // rung 3: I/O queue-creation SQE encodings
    build_set_nqueues(sqe, 0, 0, 0x10);
    if (sqe[0]  != 0x00100009u) return 21;   // SET FEATURES | CID 0x10
    if (sqe[10] != 0x07u)       return 22;   // FID = Number of Queues
    if (sqe[11] != 0u)          return 23;   // request 1 SQ + 1 CQ (0-based)
    build_create_io_cq(sqe, 0x00000001CAFE0000ULL, 1, 7, 0x11);
    if (sqe[0]  != 0x00110005u) return 24;   // CREATE IO CQ | CID 0x11
    if (sqe[6]  != 0xCAFE0000u) return 25;   // PRP1 low
    if (sqe[7]  != 0x00000001u) return 26;   // PRP1 high
    if (sqe[10] != 0x00070001u) return 27;   // qsize0=7 | qid=1
    if (sqe[11] != 0x1u)        return 28;   // PC=1
    build_create_io_sq(sqe, 0x00000002BEEF0000ULL, 1, 7, 1, 0x12);
    if (sqe[0]  != 0x00120001u) return 29;   // CREATE IO SQ | CID 0x12
    if (sqe[10] != 0x00070001u) return 30;   // qsize0=7 | qid=1
    if (sqe[11] != 0x00010001u) return 31;   // cqid=1 | PC=1

    // rung 4: NVM I/O SQE encoding (READ / WRITE, SLBA split, NLB, PRP2)
    build_io_rw(sqe, 0, 1, 0x000000123456789AULL, 0x000000001BEEF000ULL, 0, 0, 0x20);
    if (sqe[0]  != 0x00200002u) return 32;   // READ (0x02) | CID 0x20
    if (sqe[1]  != 1u)          return 33;   // NSID
    if (sqe[6]  != 0x1BEEF000u) return 34;   // PRP1 low
    if (sqe[7]  != 0u)          return 35;   // PRP1 high
    if (sqe[8]  != 0u)          return 36;   // PRP2 low (single page)
    if (sqe[10] != 0x3456789Au) return 37;   // SLBA low
    if (sqe[11] != 0x00000012u) return 38;   // SLBA high
    if (sqe[12] != 0u)          return 39;   // NLB (0-based = 1 block)
    build_io_rw(sqe, 1, 1, 0, 0x00000000CAFE0000ULL, 0, 0, 0x21);
    if (sqe[0]  != 0x00210001u) return 40;   // WRITE (0x01) | CID 0x21

    // rung D: PRP list + NLB encoding for batched multi-block I/O (pure)
    uint64_t p1 = 0, p2 = 0, katlist[16];
    for (int i = 0; i < 16; i++) katlist[i] = 0;
    uint64_t B = 0x00000000C0DE0000ULL;                 // a page-aligned buffer base
    if (nvme_build_prps(B, 1, 512, katlist, &p1, &p2) != 1) return 41;   // 512 B -> 1 page
    if (p1 != B || p2 != 0)                                 return 42;
    if (nvme_build_prps(B, 8, 512, katlist, &p1, &p2) != 1) return 43;   // 4096 B = exactly 1 page
    if (p2 != 0)                                            return 44;
    if (nvme_build_prps(B, 9, 512, katlist, &p1, &p2) != 2) return 45;   // 4608 B -> 2 pages
    if (p2 != B + 4096)                                     return 46;   // PRP2 = 2nd page directly
    if (nvme_build_prps(B, 16, 512, katlist, &p1, &p2) != 2) return 47;  // 8192 B = 2 pages
    if (p2 != B + 4096)                                     return 48;
    if (nvme_build_prps(B, 17, 512, katlist, &p1, &p2) != 3) return 49;  // 8704 B -> 3 pages -> list
    if (p2 != (uint64_t)katlist)                           return 50;
    if (katlist[0] != B + 4096)                            return 51;
    if (katlist[1] != B + 8192)                            return 52;
    if (nvme_build_prps(B, 128, 512, katlist, &p1, &p2) != 16) return 53; // 64 KB = 16 pages
    if (katlist[0]  != B + 4096)                           return 54;
    if (katlist[14] != B + (uint64_t)15 * 4096)            return 55;    // 15 list entries (pages 1..15)
    if (nvme_build_prps(B, 1, 4096, katlist, &p1, &p2) != 1) return 56;  // 4 KB block -> 1 page
    if (p2 != 0)                                            return 57;
    if (nvme_build_prps(B, 3, 4096, katlist, &p1, &p2) != 3) return 58;  // 12 KB -> 3 pages -> list
    if (katlist[0] != B + 4096 || katlist[1] != B + 8192)  return 59;
    // build_io_rw carries PRP2 (CDW8-9) + a non-zero NLB
    build_io_rw(sqe, 1, 1, 0, B, B + 4096, 7, 0x21);
    if (sqe[6]  != 0xC0DE0000u) return 60;   // PRP1 low
    if (sqe[8]  != 0xC0DE1000u) return 61;   // PRP2 low (2nd page)
    if (sqe[9]  != 0u)          return 62;   // PRP2 high
    if (sqe[12] != 7u)          return 63;   // NLB (0-based) = 8 blocks
    return 0;
}
