#ifndef ACPI_H
#define ACPI_H

#include "types.h"

/* Parse the ACPI RSDP (found via Limine) and locate the first IOAPIC.
 * Returns the physical MMIO base, or 0 when none is found. */
uint64_t acpi_find_ioapic(void);

/* GSI base of the first IOAPIC (0 on most systems). */
uint32_t acpi_ioapic_gsi_base(void);

/* Map an ISA IRQ (0-15) to its IOAPIC GSI using MADT Interrupt Source
 * Overrides. Returns the IRQ itself when no override exists (identity). */
int acpi_irq_to_gsi(int irq);

/* Number of IOAPIC redirection entries for the found IOAPIC (0 if none). */
int acpi_ioapic_entries(void);

/* ACPI-reported polarity/trigger for legacy IRQ (0=default: edge/active-high). */
void acpi_irq_overrides(int irq, int *is_level, int *active_low);

#endif