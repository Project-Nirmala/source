#include <pci.h>
#include <io.h>

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off)
{
    outl(PCI_ADDR, 0x80000000u | (uint32_t)bus << 16 | (uint32_t)dev << 11 |
                   (uint32_t)fn << 8 | (off & 0xFC));
    return inl(PCI_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v)
{
    outl(PCI_ADDR, 0x80000000u | (uint32_t)bus << 16 | (uint32_t)dev << 11 |
                   (uint32_t)fn << 8 | (off & 0xFC));
    outl(PCI_DATA, v);
}

static bool pci_scan(uint16_t vendor, uint16_t device, int32_t classcode, struct pci_dev *out)
{
    unsigned bus, dev, fn;
    int i;

    for (bus = 0; bus < 256; bus++) {
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0);
            unsigned nfn = 1;

            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            if (pci_read32((uint8_t)bus, (uint8_t)dev, 0, 0x0C) & 0x00800000)
                nfn = 8;
            for (fn = 0; fn < nfn; fn++) {
                id = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0);
                if (classcode >= 0) {
                    if ((pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08) >> 8) != (uint32_t)classcode)
                        continue;
                } else if ((id & 0xFFFF) != vendor || (id >> 16) != device) {
                    continue;
                }
                out->bus = (uint8_t)bus;
                out->dev = (uint8_t)dev;
                out->fn = (uint8_t)fn;
                out->vendor = (uint16_t)(id & 0xFFFF);
                out->device = (uint16_t)(id >> 16);
                for (i = 0; i < 6; i++)
                    out->bar[i] = pci_read32((uint8_t)bus, (uint8_t)dev, (uint8_t)fn,
                                             (uint8_t)(0x10 + 4 * i));
                return true;
            }
        }
    }
    return false;
}

bool pci_find(uint16_t vendor, uint16_t device, struct pci_dev *out)
{
    return pci_scan(vendor, device, -1, out);
}

bool pci_find_class(uint32_t classcode, struct pci_dev *out)
{
    return pci_scan(0, 0, (int32_t)classcode, out);
}

void pci_enable_io_master(const struct pci_dev *d)
{
    uint32_t cmd = pci_read32(d->bus, d->dev, d->fn, 0x04);

    cmd |= 0x1 | 0x4 | 0x400;
    pci_write32(d->bus, d->dev, d->fn, 0x04, cmd);
}
