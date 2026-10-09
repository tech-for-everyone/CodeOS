#include "pci.h"
#include "io.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

#define ECAM_BASE 0x4010000000ULL

static int pci_initialized;
static int pci_dev_count;
static struct { uint8_t bus, dev, func; uint16_t vendor, device; } pci_devs[256];

static volatile uint32_t *pci_ecam_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    return (volatile uint32_t *)(ECAM_BASE | ((uint32_t)bus << 20) | ((uint32_t)dev << 15) | ((uint32_t)func << 12) | reg);
}

void pci_init(void) {
    pci_initialized = 1;
    pci_dev_count = 0;
}

static void pci_scan_device(uint8_t bus, uint8_t dev, uint8_t func) {
    if (pci_dev_count >= 256) return;
    uint32_t id = pci_read_config(bus, dev, func, 0);
    pci_devs[pci_dev_count].bus = bus;
    pci_devs[pci_dev_count].dev = dev;
    pci_devs[pci_dev_count].func = func;
    pci_devs[pci_dev_count].vendor = id & 0xFFFF;
    pci_devs[pci_dev_count].device = id >> 16;
    pci_dev_count++;
}

void pci_scan(void) {
    pci_dev_count = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read_config(bus, dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF) continue; /* no device at func 0 */
            pci_scan_device(bus, dev, 0);
            /* Check for multi-function (bit 23 of header type) */
            uint32_t hdr = pci_read_config(bus, dev, 0, 0x0C);
            if (hdr & (1 << 23)) {
                for (int func = 1; func < 8; func++) {
                    id = pci_read_config(bus, dev, func, 0);
                    if ((id & 0xFFFF) != 0xFFFF)
                        pci_scan_device(bus, dev, func);
                }
            }
        }
    }
}

int pci_device_count(void) { return pci_dev_count; }

void pci_print_devices(void) {
    for (int i = 0; i < pci_dev_count; i++) {
        kprintf("  %02x:%02x.%x %04x:%04x\n",
                pci_devs[i].bus, pci_devs[i].dev, pci_devs[i].func,
                pci_devs[i].vendor, pci_devs[i].device);
    }
}

uint32_t pci_read_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    return *pci_ecam_addr(bus, dev, func, reg & 0xFC);
}

void pci_write_config(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val) {
    *pci_ecam_addr(bus, dev, func, reg & 0xFC) = val;
}

int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *bus, uint8_t *slot, uint8_t *func) {
    (void)bus; (void)slot; (void)func;
    for (int i = 0; i < pci_dev_count; i++) {
        if (pci_devs[i].vendor == vendor && pci_devs[i].device == device) {
            *bus = pci_devs[i].bus;
            *slot = pci_devs[i].dev;
            *func = pci_devs[i].func;
            return 1;
        }
    }
    return 0;
}

int pci_find_class(uint8_t cls, uint8_t sub,
                   uint8_t *bus, uint8_t *slot, uint8_t *func) {
    for (int i = 0; i < pci_dev_count; i++) {
        uint32_t rev = pci_read_config(pci_devs[i].bus, pci_devs[i].dev, pci_devs[i].func, 0x08);
        uint8_t c = (rev >> 24) & 0xFF;
        uint8_t s = (rev >> 16) & 0xFF;
        if (c == cls && s == sub) {
            *bus = pci_devs[i].bus;
            *slot = pci_devs[i].dev;
            *func = pci_devs[i].func;
            return 1;
        }
    }
    return 0;
}

int pci_find_class_idx(uint8_t cls, uint8_t sub, int idx,
                       uint8_t *bus, uint8_t *slot, uint8_t *func) {
    int found = 0;
    for (int i = 0; i < pci_dev_count; i++) {
        uint32_t rev = pci_read_config(pci_devs[i].bus, pci_devs[i].dev, pci_devs[i].func, 0x08);
        uint8_t c = (rev >> 24) & 0xFF;
        uint8_t s = (rev >> 16) & 0xFF;
        if (c == cls && s == sub) {
            if (found == idx) {
                *bus = pci_devs[i].bus;
                *slot = pci_devs[i].dev;
                *func = pci_devs[i].func;
                return 1;
            }
            found++;
        }
    }
    return 0;
}

uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar) {
    return pci_read_config(bus, slot, func, 0x10 + bar * 4);
}
