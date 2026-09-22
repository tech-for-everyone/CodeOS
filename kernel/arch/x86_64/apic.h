#ifndef APIC_H
#define APIC_H

#include "types.h"

void apic_init(void);
void apic_eoi(void);
uint32_t apic_get_id(void);
int apic_enabled(void);

#endif
