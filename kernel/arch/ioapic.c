#include "../arch/x86_64/apic.h"
#include "../arch/x86_64/ioapic.h"

/* use_x2apic is set in apic_init() (arch/apic.c). */
extern int use_x2apic;

/* use_x2apic is set in apic_init() (arch/apic.c). */
extern int use_x2apic;
#include "../arch/x86_64/acpi.h"
#include "../arch/x86_64/io.h"
#include "../kernel/vmm.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"

#define IOAPIC_REG_SEL       0x00
#define IOAPIC_REG_WIN       0x10
#define IOAPIC_REG_ID        0x00
#define IOAPIC_REG_VER       0x01
#define IOAPIC_REG_REDTBL    0x10

/* Redirection entry bits */
#define RTE_MASK      (1ULL << 16)
#define RTE_TRIGGER   (1ULL << 15)   /* 1 = level, 0 = edge */
#define RTE_ACTIVE    (1ULL << 13)   /* 1 = active-low, 0 = active-high */
#define RTE_DEST_LAPIC_SHIFT_X2APIC 56
#define RTE_DEST_LAPIC_SHIFT_XAPIC 24

/* 8259 PIC mask registers */
#define PIC1_DATA 0x21
#define PIC2_DATA 0xA1

static volatile uint32_t *ioapic_mmio;
static int ioapic_ready;
static int ioapic_redir_count = 24;
static uint32_t ioapic_gsi_base;

/* Diagnostic: set via "noioapic" kernel cmdline to keep the 8259 PIC path. */
int g_no_ioapic;

static inline uint32_t ioapic_reg(uint8_t reg) {
    ioapic_mmio[0] = reg;
    return ioapic_mmio[4];
}

static inline void ioapic_reg_write(uint8_t reg, uint32_t val) {
    ioapic_mmio[0] = reg;
    ioapic_mmio[4] = val;
}

static void ioapic_write_rte(int idx, uint64_t rte) {
    ioapic_reg_write(IOAPIC_REG_REDTBL + idx * 2, (uint32_t)rte);
    ioapic_reg_write(IOAPIC_REG_REDTBL + idx * 2 + 1, (uint32_t)(rte >> 32));
}

static uint64_t ioapic_read_rte(int idx) {
    uint64_t lo = ioapic_reg(IOAPIC_REG_REDTBL + idx * 2);
    uint64_t hi = ioapic_reg(IOAPIC_REG_REDTBL + idx * 2 + 1);
    return lo | (hi << 32);
}

/* Translate an ISA IRQ to the IOAPIC redirection index via the MADT GSI map. */
static int irq_to_redir(int irq) {
    if (!ioapic_ready || irq < 0 || irq >= 16) return -1;
    int gsi = acpi_irq_to_gsi(irq);
    int idx = gsi - (int)ioapic_gsi_base;
    if (idx < 0 || idx >= ioapic_redir_count) return -1;
    return idx;
}

void ioapic_mask(int irq) {
    int idx = irq_to_redir(irq);
    if (idx < 0) return;
    ioapic_write_rte(idx, ioapic_read_rte(idx) | RTE_MASK);
}

void ioapic_unmask(int irq) {
    int idx = irq_to_redir(irq);
    if (idx < 0) return;
    ioapic_write_rte(idx, ioapic_read_rte(idx) & ~RTE_MASK);
}

int ioapic_enabled(void) {
    return ioapic_ready;
}

/* Debug: read back and print all redirection entries (used during bring-up). */
void ioapic_debug_dump(void) {
    if (!ioapic_ready) { kprintf("ioapic: not ready\n"); return; }
    kprintf("ioapic: id=0x%x ver=0x%x redirs=%d gsi_base=%u\n",
            ioapic_reg(IOAPIC_REG_ID), ioapic_reg(IOAPIC_REG_VER),
            ioapic_redir_count, ioapic_gsi_base);
    for (int i = 0; i < ioapic_redir_count; i++) {
        uint64_t rte = ioapic_read_rte(i);
        kprintf("ioapic: rte[%2d] vec=0x%02x mask=%d trig=%d pol=%d dest=0x%02x\n",
                i, (unsigned)(rte & 0xFF), (int)((rte >> 16) & 1),
                (int)((rte >> 15) & 1), (int)((rte >> 13) & 1),
                (unsigned)((rte >> 56) & 0xFF));
    }
}

int ioapic_init(void) {
    if (g_no_ioapic)
        return -1;

    uint64_t base = acpi_find_ioapic();
    if (!base)
        return -1;

    ioapic_mmio = (volatile uint32_t *)phys_to_virt(base);
    ioapic_gsi_base = acpi_ioapic_gsi_base();

    /* Read the version register: bits 23:16 hold max redirection entry. */
    uint32_t ver = ioapic_reg(IOAPIC_REG_VER);
    ioapic_redir_count = ((ver >> 16) & 0xFF) + 1;
    if (ioapic_redir_count > 64) ioapic_redir_count = 64;

    uint32_t lapic_id = apic_get_id();

    /* Mask every entry first — BIOS/bus masters may have left garbage. */
    for (int i = 0; i < ioapic_redir_count; i++)
        ioapic_write_rte(i, RTE_MASK);

    /* Route each ISA IRQ through its mapped GSI (MADT ISO overrides; on PC
     * chipsets PIT IRQ0 usually lands on GSI 2). Vector stays 0x20+IRQ so the
     * IDT dispatcher maps it back to the right handler. Entries start masked;
     * irq_register() unmasks as drivers claim IRQs.
     *
     * Important: track claimed GSI slots to avoid overwrites.  On QEMU q35
     * the default identity mapping puts IRQ 2 at GSI 2, but the MADT ISO
     * override also maps IRQ 0 → GSI 2.  Processing IRQ 2 after IRQ 0 would
     * overwrite PIT's vector 0x20 with 0x22, silently breaking the timer. */
    uint8_t gsi_claimed[64];
    memset(gsi_claimed, 0, sizeof(gsi_claimed));
    for (int irq = 0; irq < 16; irq++) {
        int gsi = acpi_irq_to_gsi(irq);
        int idx = gsi - (int)ioapic_gsi_base;
        if (idx < 0 || idx >= ioapic_redir_count)
            continue;
        if (gsi_claimed[idx])
            continue;
        gsi_claimed[idx] = 1;

        int is_level = 0, active_low = 0;
        acpi_irq_overrides(irq, &is_level, &active_low);

        uint64_t rte = RTE_MASK;                 /* start masked */
        rte |= (uint64_t)(0x20 + irq);           /* vector */
        if (is_level)    rte |= RTE_TRIGGER;
        if (active_low)  rte |= RTE_ACTIVE;
        rte |= ((uint64_t)lapic_id << (use_x2apic ? RTE_DEST_LAPIC_SHIFT_X2APIC : RTE_DEST_LAPIC_SHIFT_XAPIC));

        ioapic_write_rte(idx, rte);
    }

    ioapic_ready = 1;

    /* The IOAPIC now owns legacy IRQ delivery: fully mask the 8259 PIC and
     * shut off the LINT0 ExtINT path so IRQs aren't delivered twice. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    apic_mask_lint0();

    kprintf("ioapic: initialized at phys 0x%llx (gsi base %u, %d redirs, lapic id %u x2apic=%d)\n",
            (unsigned long long)base, ioapic_gsi_base, ioapic_redir_count, lapic_id, use_x2apic);
    return 0;
}