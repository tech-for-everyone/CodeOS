#ifndef PCI_H
#define PCI_H

#include "types.h"

void pci_init(void);
void pci_scan(void);
int  pci_device_count(void);
void pci_print_devices(void);

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
int  pci_find_device(uint16_t vendor, uint16_t device,
                     uint8_t *bus, uint8_t *slot, uint8_t *func);
int  pci_find_class(uint8_t cls, uint8_t sub,
                    uint8_t *bus, uint8_t *slot, uint8_t *func);
int  pci_find_class_idx(uint8_t cls, uint8_t sub, int idx,
                        uint8_t *bus, uint8_t *slot, uint8_t *func);
uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);

#endif
