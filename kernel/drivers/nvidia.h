#ifndef NVIDIA_H
#define NVIDIA_H

#include "types.h"

/*
 * NVIDIA GPU detection driver.
 * Probes PCI for NVIDIA display controllers, reads BAR0,
 * and identifies the GPU family/model.
 * Does NOT implement modesetting — that's a future project.
 */

#define NVIDIA_VENDOR_ID 0x10DE

typedef struct {
    uint8_t  bus, slot, func;
    uint16_t device_id;
    uint32_t bar0_phys;     /* BAR0 physical address */
    uint32_t bar0_size;     /* BAR0 size in bytes */
    char     name[48];      /* Human-readable GPU name */
    int      found;
} nvidia_gpu_info_t;

/* Probe for NVIDIA GPUs. Populates info for the first one found.
 * Returns 1 if found, 0 if not. */
int nvidia_probe(nvidia_gpu_info_t *info);

/* Print GPU info to kernel log */
void nvidia_print_info(const nvidia_gpu_info_t *info);

/* Returns the number of NVIDIA GPUs detected by PCI scan */
int nvidia_count(void);

#endif
