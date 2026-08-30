#include "../../core/kernel.h"
#include "pci.h"

// --- PCI config space, mechanism #1 (0xCF8 address / 0xCFC data) --------------

uint32_t pci_cfg_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    return ((uint32_t)bus  << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)func <<  8) | ((uint32_t)off & 0xFC) | 0x80000000u;
}

uint32_t pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(0xCF8, pci_cfg_address(bus, slot, func, off));
    return inl(0xCFC);
}

void pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val) {
    outl(0xCF8, pci_cfg_address(bus, slot, func, off));
    outl(0xCFC, val);
}

// --- class-code -> human label ------------------------------------------------

const char* pci_class_name(uint8_t cls, uint8_t sub, uint8_t prog) {
    switch (cls) {
        case 0x01:                                          // mass storage
            switch (sub) {
                case 0x01: return "IDE controller";
                case 0x06: return (prog == 0x01) ? "SATA AHCI controller" : "SATA controller";
                case 0x08: return (prog == 0x02) ? "NVMe controller"
                                                  : "Non-Volatile memory controller";
                case 0x00: return "SCSI controller";
                default:   return "Mass storage controller";
            }
        case 0x02: return "Ethernet/Network controller";
        case 0x03: return "Display/VGA controller";
        case 0x04: return "Multimedia controller";
        case 0x05: return "Memory controller";
        case 0x06:                                          // bridge
            switch (sub) {
                case 0x00: return "Host bridge";
                case 0x01: return "ISA bridge";
                case 0x04: return "PCI-to-PCI bridge";
                default:   return "Bridge";
            }
        case 0x07: return "Communication controller";       // serial/modem
        case 0x08: return "Base system peripheral";         // PIC/DMA/timer/RTC
        case 0x09: return "Input device controller";        // keyboard/mouse/etc.
        case 0x0B: return "Processor";                      // co-processor
        case 0x0C:                                          // serial bus
            return (sub == 0x03) ? "USB controller" : "Serial bus controller";
        case 0x0D: return "Wireless controller";            // e.g. an Intel AX200 Wi-Fi
        case 0x10: return "Encryption controller";
        default:   return "PCI device";
    }
}

// --- bus scan -----------------------------------------------------------------

int pci_enumerate(pci_dev_t* out, int max) {
    int n = 0;
    for (int bus = 0; bus < 256 && n < max; bus++) {
        for (int slot = 0; slot < 32 && n < max; slot++) {
            uint32_t v0 = pci_cfg_read32((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            if ((v0 & 0xFFFF) == 0xFFFF) continue;          // no device in this slot
            uint32_t hdr   = pci_cfg_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
            int multif     = (hdr >> 16) & 0x80;            // header-type bit 7
            int funcs      = multif ? 8 : 1;
            for (int func = 0; func < funcs && n < max; func++) {
                uint32_t vd = pci_cfg_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                if ((vd & 0xFFFF) == 0xFFFF) continue;
                uint32_t cc   = pci_cfg_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                uint32_t bar0 = pci_cfg_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x10);
                pci_dev_t* d = &out[n++];
                d->bus = (uint8_t)bus; d->slot = (uint8_t)slot; d->func = (uint8_t)func;
                d->vendor     = (uint16_t)(vd & 0xFFFF);
                d->device     = (uint16_t)((vd >> 16) & 0xFFFF);
                d->revision   = (uint8_t)(cc & 0xFF);
                d->prog_if    = (uint8_t)((cc >> 8) & 0xFF);
                d->subclass   = (uint8_t)((cc >> 16) & 0xFF);
                d->class_code = (uint8_t)((cc >> 24) & 0xFF);
                d->bar0       = bar0;
            }
        }
    }
    return n;
}

// --- KAT: the pure logic (address packing + class decode), no hardware --------

int pci_selftest(void) {
    // mechanism-#1 config-address packing (enable bit 31, offset dword-aligned)
    struct { uint8_t b, s, f, o; uint32_t want; } av[] = {
        {0,  0, 0, 0x00, 0x80000000u},
        {0,  2, 0, 0x10, 0x80001010u},
        {0, 31, 7, 0xFC, 0x8000FFFCu},
        {1,  0, 0, 0x00, 0x80010000u},
        {0,  3, 0, 0x0E, 0x8000180Cu},   // offset masked to 0x0C
    };
    for (int i = 0; i < (int)(sizeof(av) / sizeof(av[0])); i++)
        if (pci_cfg_address(av[i].b, av[i].s, av[i].f, av[i].o) != av[i].want) return 10 + i;

    // (class, subclass, prog-if) -> label, esp. the storage controllers an installer must
    // tell apart (NVMe vs AHCI vs IDE) and the other classes a real machine reports (Wi-Fi,
    // base-system peripherals, USB) so `lspci` names them instead of a bare "PCI device".
    struct { uint8_t c, s, p; const char* want; } cv[] = {
        {0x01, 0x08, 0x02, "NVMe controller"},
        {0x01, 0x06, 0x01, "SATA AHCI controller"},
        {0x01, 0x01, 0x00, "IDE controller"},
        {0x02, 0x00, 0x00, "Ethernet/Network controller"},
        {0x03, 0x00, 0x00, "Display/VGA controller"},
        {0x06, 0x00, 0x00, "Host bridge"},
        {0x0C, 0x03, 0x30, "USB controller"},
        {0x0D, 0x80, 0x00, "Wireless controller"},          // Intel AX200-class Wi-Fi
        {0x08, 0x00, 0x00, "Base system peripheral"},
        {0x07, 0x00, 0x00, "Communication controller"},
        {0x09, 0x00, 0x00, "Input device controller"},
        {0x99, 0x00, 0x00, "PCI device"},                   // unknown class -> generic label
    };
    for (int i = 0; i < (int)(sizeof(cv) / sizeof(cv[0])); i++)
        if (strcmp(pci_class_name(cv[i].c, cv[i].s, cv[i].p), cv[i].want) != 0) return 40 + i;
    return 0;
}
