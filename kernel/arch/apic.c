#include "../arch/x86_64/apic.h"
#include "../kernel/vmm.h"
#include "../kernel/string.h"

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_ENABLE   (1ULL << 11)

#define APIC_BASE_PHYS     0xFEE00000ULL

#define APIC_REG_ID        0x020
#define APIC_REG_TPR       0x080
#define APIC_REG_EOI       0x0B0
#define APIC_REG_SVR       0x0F0
#define APIC_REG_LINT0     0x350
#define APIC_REG_LINT1     0x360

#define APIC_LVT_EXTINT    0x700
#define APIC_LVT_NMI       0x400

static volatile uint32_t *apic_mmio;
static int apic_ready;

static inline uint64_t apic_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void apic_wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" : : "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

static inline uint32_t apic_read(uint32_t reg) {
    return apic_mmio[reg >> 2];
}

static inline void apic_write(uint32_t reg, uint32_t val) {
    apic_mmio[reg >> 2] = val;
    (void)apic_mmio[reg >> 2];
}

void apic_init(void) {
    uint64_t base = apic_rdmsr(IA32_APIC_BASE_MSR);
    if (!(base & APIC_BASE_ENABLE))
        apic_wrmsr(IA32_APIC_BASE_MSR, base | APIC_BASE_ENABLE);

    apic_mmio = (volatile uint32_t *)phys_to_virt(APIC_BASE_PHYS);

    /* Software-enable the APIC with a spurious vector, then route the 8259
     * PIC through LINT0 (ExtINT) so legacy IRQs (PIT timer, keyboard, etc.)
     * keep flowing while MSI-capable devices can still use the LAPIC. */
    apic_write(APIC_REG_SVR, apic_read(APIC_REG_SVR) | 0x100 | 0xFF);
    apic_write(APIC_REG_LINT0, APIC_LVT_EXTINT);
    apic_write(APIC_REG_LINT1, APIC_LVT_NMI);

    /* Accept every priority class. */
    apic_write(APIC_REG_TPR, 0);
    apic_write(APIC_REG_EOI, 0);

    apic_ready = 1;
}

void apic_eoi(void) {
    if (!apic_ready) return;
    apic_write(APIC_REG_EOI, 0);
}

uint32_t apic_get_id(void) {
    if (!apic_ready) return 0;
    return apic_read(APIC_REG_ID) >> 24;
}

int apic_enabled(void) {
    return apic_ready;
}
