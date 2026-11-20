#ifndef _NIRMALA_PCI_H
#define _NIRMALA_PCI_H

#include <types.h>

struct pci_dev {
    uint8_t  bus, dev, fn;
    uint16_t vendor, device;
    uint32_t bar[6];
};

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
void     pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v);
bool     pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out);
bool     pci_find_class(uint32_t classcode, struct pci_dev *out);
void     pci_enable_io_master(const struct pci_dev *d);

#endif
