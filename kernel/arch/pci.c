#include "pci.h"
#include "io.h"
#include "kernel/kprintf.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define MAX_PCI_DEVICES 256

static int pci_dev_count;
static struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor;
    uint16_t device;
    uint8_t class;
    uint8_t subclass;
} pci_devices[MAX_PCI_DEVICES];

static uint32_t pci_read_conf(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (uint32_t)((uint32_t)1 << 31) |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) |
                    (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_read_vendor(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_read_conf(bus, slot, func, 0) & 0xFFFF);
}

static uint16_t pci_read_device(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_read_conf(bus, slot, func, 0) >> 16);
}

static uint8_t pci_read_class(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint8_t)(pci_read_conf(bus, slot, func, 8) >> 24);
}

static uint8_t pci_read_subclass(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint8_t)((pci_read_conf(bus, slot, func, 8) >> 16) & 0xFF);
}

static void add_device(uint8_t bus, uint8_t slot, uint8_t func,
                       uint16_t vendor, uint16_t device,
                       uint8_t class, uint8_t subclass) {
    if (pci_dev_count >= MAX_PCI_DEVICES) return;
    int i = pci_dev_count++;
    pci_devices[i].bus = bus;
    pci_devices[i].slot = slot;
    pci_devices[i].func = func;
    pci_devices[i].vendor = vendor;
    pci_devices[i].device = device;
    pci_devices[i].class = class;
    pci_devices[i].subclass = subclass;
}

static const char *class_name(uint8_t cls, uint8_t sub) {
    (void)sub;
    switch (cls) {
        case 0x00: return "Legacy";
        case 0x01: return "Mass storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System peripheral";
        case 0x09: return "Input device";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial bus";
        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent IO";
        case 0x0F: return "Satellite comm";
        case 0x10: return "Encryption";
        case 0x11: return "Signal processing";
        case 0x12: return "Processing accelerator";
        case 0x13: return "Non-essential instrumentation";
        case 0x40: return "Co-processor";
        case 0xFF: return "Unassigned";
        default:   return "Unknown";
    }
}

static void check_bus(uint8_t bus) {
    for (int slot = 0; slot < 32; slot++) {
        for (int func = 0; func < 8; func++) {
            uint16_t vendor = pci_read_vendor(bus, slot, func);
            if (vendor == 0xFFFF) {
                if (func == 0) break;
                continue;
            }
            add_device(bus, slot, func, vendor,
                       pci_read_device(bus, slot, func),
                       pci_read_class(bus, slot, func),
                       pci_read_subclass(bus, slot, func));

            /* if multifunction, continue scanning functions */
            if (func == 0) {
                uint32_t hdr = pci_read_conf(bus, slot, func, 0x0C);
                if (!(hdr & 0x800000)) break;
            }
        }
    }
}

void pci_init(void) {
    pci_dev_count = 0;
}

/* ── PCI capability traversal ── */
uint8_t pci_find_cap(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id) {
    uint32_t status = pci_read_conf(bus, slot, func, 0x04);
    if (!(status & 0x00100000)) return 0;
    uint8_t ptr = (uint8_t)((pci_read_conf(bus, slot, func, 0x34) & 0xFF));
    while (ptr != 0) {
        uint32_t cap = pci_read_conf(bus, slot, func, ptr);
        if ((cap & 0xFF) == cap_id) return ptr;
        ptr = (uint8_t)((cap >> 8) & 0xFF);
    }
    return 0;
}

/* ── MSI ── */
int pci_msi_enable(uint8_t bus, uint8_t slot, uint8_t func,
                   uint8_t vec, uint64_t cpu_msi_addr) {
    uint8_t cap = pci_find_cap(bus, slot, func, 0x05);
    if (!cap) return -1;

    uint32_t flags = pci_read_conf(bus, slot, func, cap + 2);
    int is_64 = (flags & 0x0080) ? 1 : 0;

    uint32_t msg_addr = (uint32_t)(cpu_msi_addr & 0xFFFFFFFF);
    pci_config_write(bus, slot, func, cap + 4, msg_addr);
    if (is_64)
        pci_config_write(bus, slot, func, cap + 8, (uint32_t)(cpu_msi_addr >> 32));

    uint32_t msg_data = vec & 0xFF;
    if (is_64)
        pci_config_write(bus, slot, func, cap + 0x0C, msg_data);
    else
        pci_config_write(bus, slot, func, cap + 0x08, msg_data);

    uint16_t msi_flags = (uint16_t)(pci_read_conf(bus, slot, func, cap + 2) & 0xFFFF);
    msi_flags |= 0x0001;
    pci_config_write(bus, slot, func, cap + 2, msi_flags);
    return 0;
}

void pci_msi_disable(uint8_t bus, uint8_t slot, uint8_t func) {
    uint8_t cap = pci_find_cap(bus, slot, func, 0x05);
    if (!cap) return;
    uint16_t flags = (uint16_t)(pci_read_conf(bus, slot, func, cap + 2) & 0xFFFF);
    flags &= ~0x0001;
    pci_config_write(bus, slot, func, cap + 2, flags);
}

/* ── MSI-X ── */
int pci_msix_enable(uint8_t bus, uint8_t slot, uint8_t func, int entries) {
    uint8_t cap = pci_find_cap(bus, slot, func, 0x11);
    if (!cap) return -1;
    uint16_t flags = (uint16_t)(pci_read_conf(bus, slot, func, cap + 2) & 0xFFFF);
    int table_size = (flags & 0x07FF) + 1;
    if (entries > table_size) entries = table_size;
    flags |= 0x8000;
    flags &= ~0x4000;
    pci_config_write(bus, slot, func, cap + 2, flags);
    return entries;
}

uint32_t pci_msix_table_info(uint8_t bus, uint8_t slot, uint8_t func,
                             int *bir, uint32_t *offset) {
    uint8_t cap = pci_find_cap(bus, slot, func, 0x11);
    if (!cap) return 0;
    uint32_t tbl = pci_read_conf(bus, slot, func, cap + 4);
    if (bir) *bir = tbl & 0x07;
    if (offset) *offset = tbl & 0xFFFFFFF8;
    return cap;
}

/* ── Bridge scan ── */
static void pci_scan_bus(uint8_t bus) {
    check_bus(bus);
    for (int slot = 0; slot < 32; slot++) {
        uint16_t v = pci_read_vendor(bus, slot, 0);
        if (v == 0xFFFF) continue;
        uint8_t hdr = (uint8_t)((pci_read_conf(bus, slot, 0, 0x0C) >> 16) & 0xFF);
        if ((hdr & 0x7F) == 0x01) {
            uint32_t bus_reg = pci_read_conf(bus, slot, 0, 0x18);
            uint8_t sec_bus = (uint8_t)((bus_reg >> 8) & 0xFF);
            if (sec_bus != 0 && sec_bus != bus)
                pci_scan_bus(sec_bus);
        }
    }
}

void pci_scan(void) {
    pci_dev_count = 0;

    uint16_t vendor = pci_read_vendor(0, 0, 0);
    if (vendor == 0xFFFF) {
        kprintf("PCI: no host bridge found\n");
        return;
    }

    uint8_t hdr_type = (uint8_t)((pci_read_conf(0, 0, 0, 0x0C) >> 16) & 0xFF);
    if (hdr_type & 0x80) {
        for (int func = 0; func < 8; func++) {
            vendor = pci_read_vendor(0, 0, func);
            if (vendor != 0xFFFF)
                pci_scan_bus(func);
        }
    } else {
        pci_scan_bus(0);
    }

    kprintf("PCI: %d devices found\n", pci_dev_count);
}

int pci_device_count(void) { return pci_dev_count; }

void pci_print_devices(void) {
    if (pci_dev_count == 0) {
        kprintf("No PCI devices found (run 'pci scan' first)\n");
        return;
    }
    for (int i = 0; i < pci_dev_count; i++) {
        kprintf("%02x:%02x.%x  %04x:%04x  %s\n",
                pci_devices[i].bus,
                pci_devices[i].slot,
                pci_devices[i].func,
                pci_devices[i].vendor,
                pci_devices[i].device,
                class_name(pci_devices[i].class, pci_devices[i].subclass));
    }
    kprintf("(%d devices)\n", pci_dev_count);
}

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_read_conf(bus, slot, func, offset);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (uint32_t)((uint32_t)1 << 31) |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) |
                    (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *bus, uint8_t *slot, uint8_t *func) {
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            for (int f = 0; f < 8; f++) {
                uint32_t id = pci_read_conf(b, s, f, 0);
                uint16_t v = (uint16_t)(id & 0xFFFF);
                uint16_t d = (uint16_t)(id >> 16);
                if (v != 0xFFFF && v == vendor && d == device) {
                    if (bus) *bus = b;
                    if (slot) *slot = s;
                    if (func) *func = f;
                    return 1;
                }
                if (f == 0) {
                    uint32_t hdr = pci_read_conf(b, s, f, 0x0C);
                    if (!(hdr & 0x800000)) break;
                }
            }
        }
    }
    return 0;
}

int pci_find_class(uint8_t cls, uint8_t sub,
                    uint8_t *bus, uint8_t *slot, uint8_t *func) {
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            for (int f = 0; f < 8; f++) {
                uint16_t v = pci_read_vendor(b, s, f);
                if (v == 0xFFFF) {
                    if (f == 0) break;
                    continue;
                }
                if (pci_read_class(b, s, f) == cls && pci_read_subclass(b, s, f) == sub) {
                    if (bus) *bus = b;
                    if (slot) *slot = s;
                    if (func) *func = f;
                    return 1;
                }
                if (f == 0) {
                    uint32_t hdr = pci_read_conf(b, s, f, 0x0C);
                    if (!(hdr & 0x800000)) break;
                }
            }
        }
    }
    return 0;
}

int pci_find_class_idx(uint8_t cls, uint8_t sub, int idx,
                        uint8_t *bus, uint8_t *slot, uint8_t *func) {
    int found = 0;
    for (int b = 0; b < 256; b++) {
        for (int s = 0; s < 32; s++) {
            for (int f = 0; f < 8; f++) {
                uint16_t v = pci_read_vendor(b, s, f);
                if (v == 0xFFFF) {
                    if (f == 0) break;
                    continue;
                }
                if (pci_read_class(b, s, f) == cls && pci_read_subclass(b, s, f) == sub) {
                    if (found == idx) {
                        if (bus) *bus = b;
                        if (slot) *slot = s;
                        if (func) *func = f;
                        return 1;
                    }
                    found++;
                }
                if (f == 0) {
                    uint32_t hdr = pci_read_conf(b, s, f, 0x0C);
                    if (!(hdr & 0x800000)) break;
                }
            }
        }
    }
    return 0;
}

uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar) {
    return pci_read_conf(bus, slot, func, 0x10 + bar * 4);
}
