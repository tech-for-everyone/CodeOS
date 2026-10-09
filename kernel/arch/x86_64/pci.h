#ifndef PCI_H
#define PCI_H

#include "types.h"

void pci_init(void);
void pci_scan(void);
int  pci_device_count(void);
void pci_print_devices(void);

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
int  pci_find_device(uint16_t vendor, uint16_t device,
                        uint8_t *bus, uint8_t *slot, uint8_t *func);
int  pci_find_class(uint8_t cls, uint8_t sub,
                        uint8_t *bus, uint8_t *slot, uint8_t *func);
int  pci_find_class_idx(uint8_t cls, uint8_t sub, int idx,
                        uint8_t *bus, uint8_t *slot, uint8_t *func);
uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);

/* Capability list */
uint8_t pci_find_cap(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id);

/* MSI */
int  pci_msi_enable(uint8_t bus, uint8_t slot, uint8_t func,
                     uint8_t vec, uint64_t cpu_msi_addr);
void pci_msi_disable(uint8_t bus, uint8_t slot, uint8_t func);

/* MSI-X */
int      pci_msix_enable(uint8_t bus, uint8_t slot, uint8_t func, int entries);
uint32_t pci_msix_table_info(uint8_t bus, uint8_t slot, uint8_t func,
                              int *bir, uint32_t *offset);

#endif
