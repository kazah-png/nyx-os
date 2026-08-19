#ifndef PCI_H
#define PCI_H

// PCI configuration-space access (mechanism #1: the 0xCF8 address / 0xCFC data
// port pair) plus a bus scanner. The first recon rung for real hardware: on a
// modern UEFI laptop the disk is an NVMe controller on PCIe (not the legacy IDE
// ports `disks`/ATA probes), so `lspci` is how we find it and read the BAR that
// a future NVMe driver will map. Reuses the same mechanism the rtl8139 driver
// already drives, exposed here as a shared, reusable helper.

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if, revision;
    uint32_t bar0;
} pci_dev_t;

// Pack a mechanism-#1 config address (enable bit 31, offset dword-aligned).
uint32_t    pci_cfg_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint32_t    pci_cfg_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void        pci_cfg_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
// Human label for a (class, subclass, prog-if) triple — knows the storage
// controllers an installer cares about (NVMe / SATA AHCI / IDE) plus the common
// device classes, falling back to the base-class name.
const char* pci_class_name(uint8_t cls, uint8_t sub, uint8_t prog);
// Brute-force scan bus 0..255 x slot 0..31 (+ multifunction), collecting up to
// `max` present devices into `out`. Bounded (no recursion). Returns the count.
int         pci_enumerate(pci_dev_t* out, int max);
int         pci_selftest(void);

#endif
