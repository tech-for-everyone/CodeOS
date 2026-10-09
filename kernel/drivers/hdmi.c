#include "hdmi.h"
#include "pci.h"
#include "kprintf.h"

#define MAX_GPU 8

typedef struct {
    uint8_t bus, slot, func;
    uint16_t vendor, device;
    uint64_t bar0;
} gpu_dev_t;

static gpu_dev_t gpu_list[MAX_GPU];
static int gpu_count;
static int gpu_active = -1;
static const char *gpu_vendor = "Generic Display";

static const char *vendor_name(uint16_t vid) {
    switch (vid) {
        case 0x8086: return "Intel";
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x1234: return "QEMU";
        case 0x1AF4: return "Red Hat";
        case 0x15AD: return "VMware";
        default:     return "Unknown";
    }
}

static int is_integrated(uint16_t vendor) {
    return vendor == 0x8086;
}

static uint64_t read_bar0(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t bar0 = pci_read_bar(bus, slot, func, 0);
    if (bar0 == 0 || (bar0 & 1))
        return 0;
    uint64_t addr = bar0 & 0xFFFFFFF0;
    if (bar0 & 0x04) {
        uint32_t bar1 = pci_read_bar(bus, slot, func, 1);
        addr |= (uint64_t)bar1 << 32;
    }
    return addr;
}

static int gpu_enum(void) {
    gpu_count = 0;
    static const uint8_t subs[] = {0x00, 0x01, 0x80};
    int idx = 0;

    while (gpu_count < MAX_GPU) {
        uint8_t bus = 0, slot = 0, func = 0;
        int found = 0;

        for (int si = 0; si < 3; si++) {
            if (pci_find_class_idx(0x03, subs[si], idx, &bus, &slot, &func)) {
                found = 1;
                break;
            }
        }
        if (!found) break;

        uint32_t id = pci_config_read(bus, slot, func, 0);
        uint16_t vendor = id & 0xFFFF;
        uint16_t device = id >> 16;
        uint64_t bar = read_bar0(bus, slot, func);

        gpu_list[gpu_count].bus = bus;
        gpu_list[gpu_count].slot = slot;
        gpu_list[gpu_count].func = func;
        gpu_list[gpu_count].vendor = vendor;
        gpu_list[gpu_count].device = device;
        gpu_list[gpu_count].bar0 = bar;

        kprintf("GPU: %s %04x:%04x at %02x:%02x.%d BAR0=0x%llx %s\n",
                vendor_name(vendor), vendor, device, bus, slot, func,
                (unsigned long long)bar,
                is_integrated(vendor) ? "(integrated)" : "(discrete)");

        gpu_count++;
        idx++;
    }

    return gpu_count;
}

const char *hdmi_gpu_name(void) {
    return gpu_vendor;
}

int hdmi_detect(uint64_t *fb_addr, uint32_t *width, uint32_t *height, uint32_t *pitch) {
    if (gpu_count == 0)
        gpu_enum();
    if (gpu_count == 0)
        return 0;

    gpu_active = -1;

    /* Try discrete GPUs first */
    for (int i = 0; i < gpu_count; i++) {
        if (!is_integrated(gpu_list[i].vendor) && gpu_list[i].bar0) {
            gpu_active = i;
            break;
        }
    }

    /* Fallback to integrated GPUs */
    if (gpu_active < 0) {
        for (int i = 0; i < gpu_count; i++) {
            if (is_integrated(gpu_list[i].vendor) && gpu_list[i].bar0) {
                gpu_active = i;
                break;
            }
        }
    }

    /* Last resort: any GPU with BAR0 */
    if (gpu_active < 0) {
        for (int i = 0; i < gpu_count; i++) {
            if (gpu_list[i].bar0) {
                gpu_active = i;
                break;
            }
        }
    }

    if (gpu_active < 0) return 0;

    gpu_dev_t *g = &gpu_list[gpu_active];
    gpu_vendor = vendor_name(g->vendor);
    *fb_addr = g->bar0;
    *width = 1920;
    *height = 1080;
    *pitch = 1920 * 4;

    kprintf("HDMI: selected %s GPU (dGPU:%s) at %02x:%02x.%d\n",
            gpu_vendor,
            is_integrated(g->vendor) ? "no" : "yes",
            g->bus, g->slot, g->func);

    return 1;
}
