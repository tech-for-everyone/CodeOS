#include "nvidia.h"
#include "../arch/x86_64/pci.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

/* Known NVIDIA device IDs (subset — device name lookup) */
static const struct { uint16_t id; const char *name; } nvidia_devices[] = {
    /* Tesla / Volta */
    { 0x1DB8, "Tesla V100" },
    { 0x1DB4, "Tesla V100" },
    { 0x1DB1, "Tesla V100 SXM2" },
    /* Ampere */
    { 0x2230, "RTX 3090" },
    { 0x2204, "RTX 3090" },
    { 0x2208, "RTX 3080" },
    { 0x2206, "RTX 3070" },
    { 0x220C, "RTX 3060" },
    { 0x2484, "RTX 3050" },
    /* Ada Lovelace */
    { 0x2684, "RTX 4090" },
    { 0x2782, "RTX 4080" },
    { 0x2786, "RTX 4070 Ti" },
    { 0x2788, "RTX 4070" },
    { 0x2482, "RTX 4060 Ti" },
    { 0x2484, "RTX 4060" },
    /* Turing */
    { 0x1E04, "RTX 2080 Ti" },
    { 0x1E87, "RTX 2080 Super" },
    { 0x1E84, "RTX 2080" },
    { 0x1E81, "RTX 2070 Super" },
    { 0x1E82, "RTX 2070" },
    { 0x1E93, "RTX 2060 Super" },
    { 0x1E89, "RTX 2060" },
    { 0x1E86, "RTX 2080 (TU104)" },
    { 0x16F8, "Quadro RTX 6000" },
    { 0x1E30, "Quadro RTX 5000" },
    /* Pascal */
    { 0x1B80, "GTX 1080 Ti" },
    { 0x1B81, "GTX 1080" },
    { 0x1B82, "GTX 1070 Ti" },
    { 0x1B83, "GTX 1070" },
    { 0x1B84, "GTX 1060 6GB" },
    { 0x1B85, "GTX 1060 3GB" },
    { 0x13C0, "GTX 1080 (GP104)" },
    /* Kepler */
    { 0x1180, "GTX 780 Ti" },
    { 0x1182, "GTX 780" },
    { 0x1184, "GTX 770" },
    { 0x1185, "GTX 760" },
    { 0x1187, "GTX 760 Ti" },
    /* GeForce MX / generic */
    { 0x1340, "GeForce MX150" },
    { 0x1341, "GeForce MX150" },
    { 0x1342, "GeForce MX250" },
    { 0x1343, "GeForce MX350" },
    /* Data Center / A-series */
    { 0x20B0, "A100" },
    { 0x2236, "A10" },
    { 0x2237, "A40" },
    { 0, 0 }
};

static const char *lookup_device(uint16_t devid) {
    for (int i = 0; nvidia_devices[i].name; i++) {
        if (nvidia_devices[i].id == devid)
            return nvidia_devices[i].name;
    }
    return 0;
}

static int nvidia_count_val = 0;

static uint32_t bar_size(uint8_t bus, uint8_t slot, uint8_t func, int bar) {
    uint32_t addr_off = 0x10 + bar * 4;
    /* Save current value */
    uint32_t orig = pci_config_read(bus, slot, func, addr_off);
    /* Write all 1s */
    pci_config_write(bus, slot, func, addr_off, 0xFFFFFFFF);
    uint32_t size = pci_config_read(bus, slot, func, addr_off);
    /* Restore original */
    pci_config_write(bus, slot, func, addr_off, orig);
    /* size is the number of bytes (inverted) */
    if (size == 0 || size == 0xFFFFFFFF) return 0;
    return ~(size & 0xFFFFFFF0) + 1;
}

int nvidia_probe(nvidia_gpu_info_t *info) {
    if (!info) return 0;
    memset(info, 0, sizeof(*info));
    nvidia_count_val = 0;

    uint16_t bus = 0;
    uint8_t slot = 0, func = 0;

    /* Scan all PCI slots for NVIDIA (0x10DE) class 0x03 (display) */
    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (func = 0; func < 8; func++) {
                uint32_t val = pci_config_read(bus, slot, func, 0x00);
                uint16_t vendor = val & 0xFFFF;
                if (vendor != NVIDIA_VENDOR_ID) continue;

                uint32_t class_reg = pci_config_read(bus, slot, func, 0x08);
                uint8_t class_code = (class_reg >> 24) & 0xFF;
                if (class_code != 0x03) continue; /* display controller */

                nvidia_count_val++;

                uint32_t devid_reg = pci_config_read(bus, slot, func, 0x00);
                uint16_t devid = (devid_reg >> 16) & 0xFFFF;

                /* Read BAR0 (typically the MMIO register space) */
                uint32_t bar0_raw = pci_config_read(bus, slot, func, 0x10);
                uint32_t bar0_phys = bar0_raw & 0xFFFFFFF0;

                /* Enable bus master + memory space + I/O space */
                uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                pci_config_write(bus, slot, func, 0x04, cmd | 0x0007);

                if (!info->found) {
                    info->bus    = bus;
                    info->slot   = slot;
                    info->func   = func;
                    info->device_id = devid;
                    info->bar0_phys = bar0_phys;
                    info->bar0_size = bar_size(bus, slot, func, 0);
                    info->found  = 1;

                    const char *name = lookup_device(devid);
                    if (name) {
                        int n = 0;
                        while (name[n] && n < (int)sizeof(info->name) - 1) {
                            info->name[n] = name[n]; n++;
                        }
                        info->name[n] = 0;
                    } else {
                        snprintf(info->name, sizeof(info->name),
                                 "NVIDIA 0x%04X", devid);
                    }
                }
            }
        }
    }

    return info->found;
}

void nvidia_print_info(const nvidia_gpu_info_t *info) {
    if (!info || !info->found) {
        kprintf("NVIDIA: no GPU found\n");
        return;
    }
    kprintf("NVIDIA: %s at %02x:%02x.%d\n",
            info->name, info->bus, info->slot, info->func);
    kprintf("  device_id=0x%04X BAR0=0x%08X size=%u KB\n",
            info->device_id, info->bar0_phys,
            info->bar0_size / 1024);
}

int nvidia_count(void) {
    return nvidia_count_val;
}
