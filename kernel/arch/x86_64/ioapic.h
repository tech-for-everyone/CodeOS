#ifndef IOAPIC_H
#define IOAPIC_H

#include "types.h"

/* Initialize the IOAPIC, routing legacy IRQs 0-15 to the BSP LAPIC.
 * Returns 0 on success, -1 when no IOAPIC is available (PIC fallback). */
int  ioapic_init(void);

/* Mask/unmask an ISA IRQ through the IOAPIC redirection entry. */
void ioapic_mask(int irq);
void ioapic_unmask(int irq);

int  ioapic_enabled(void);

#endif