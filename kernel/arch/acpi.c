#include "../arch/x86_64/acpi.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/vmm.h"

/* ── ACPI structures ──────────────────────────────────────────────── */
struct __attribute__((packed)) acpi_rsdp {
    char     signature[8];   /* "RSD PTR " */
    uint8_t  checksum;
    uint8_t  oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint32_t ext_checksum;
    uint8_t  reserved[3];
};

struct __attribute__((packed)) acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    char     creator_id[4];
    uint32_t creator_revision;
};

struct __attribute__((packed)) acpi_madt {
    struct acpi_sdt_header header;
    uint32_t lapic_addr;
    uint32_t flags;
    /* entries follow */
};

/* MADT entry types */
#define MADT_LAPIC      0x00
#define MADT_IOAPIC     0x01
#define MADT_ISO        0x02

struct __attribute__((packed)) madt_ioapic {
    uint8_t  type;
    uint8_t  length;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
};

struct __attribute__((packed)) madt_iso {
    uint8_t  type;
    uint8_t  length;
    uint8_t  bus;
    uint8_t  irq;
    uint32_t gsi;
    uint16_t flags;
};

static uint64_t g_ioapic_base;
static uint32_t g_ioapic_gsi_base;
static int      g_ioapic_entries;
/* per-IRQ overrides: stored compactly as bits */
static uint8_t  g_iso_level[16];   /* bit set -> level-triggered */
static uint8_t  g_iso_activelow[16];
static uint8_t  g_irq_to_gsi[16];  /* 0 = no override (identity) */

static int acpi_checksum_ok(const void *ptr, uint32_t len) {
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += p[i];
    return sum == 0;
}

static void acpi_parse_madt(uint64_t madt_phys) {
    struct acpi_madt *madt = (struct acpi_madt *)phys_to_virt(madt_phys);
    if (madt->header.length < sizeof(struct acpi_madt))
        return;

    uint32_t end = madt->header.length;
    uint8_t *p = (uint8_t *)madt + sizeof(struct acpi_madt);
    uint8_t *pend = (uint8_t *)madt + end;

    while (p + 2 <= pend) {
        uint8_t type = p[0];
        uint8_t len  = p[1];
        if (len < 2 || p + len > pend)
            break;

        if (type == MADT_IOAPIC && len >= sizeof(struct madt_ioapic)) {
            struct madt_ioapic *io = (struct madt_ioapic *)p;
            if (!g_ioapic_base) {
                g_ioapic_base = io->ioapic_addr;
                g_ioapic_gsi_base = io->gsi_base;
                g_ioapic_entries = 24; /* typical; refined from version reg */
                kprintf("acpi: MADT IOAPIC at phys 0x%lx gsi_base=%u\n",
                        io->ioapic_addr, io->gsi_base);
            }
        } else if (type == MADT_ISO && len >= sizeof(struct madt_iso)) {
            struct madt_iso *iso = (struct madt_iso *)p;
            kprintf("acpi: MADT ISO irq=%u gsi=%u flags=0x%x\n",
                    iso->irq, iso->gsi, iso->flags);
            if (iso->bus == 0 && iso->irq < 16) {
                /* Record the ISA IRQ -> GSI remap (classically IRQ0->GSI2 on
                 * PC chipsets). */
                if (iso->gsi < 256)
                    g_irq_to_gsi[iso->irq] = (uint8_t)iso->gsi;
                /* flags bit 1 = polarity (1 = active-low), bit 2-3 = trigger
                 * (01 = level) */
                if (iso->flags & 0x2)
                    g_iso_activelow[iso->irq] = 1;
                if ((iso->flags >> 2) & 0x3)
                    g_iso_level[iso->irq] = 1;
            }
        }

        p += len;
    }
}

uint64_t acpi_find_ioapic(void) {
    if (g_ioapic_base)
        return g_ioapic_base;

    /* Locate the RSDP through the Limine RSDP request (mapped via HHDM). */
    extern void *limine_rsdp_address(void);
    struct acpi_rsdp *rsdp = (struct acpi_rsdp *)limine_rsdp_address();
    if (!rsdp)
        return 0;

    /* Walk RSDT/XSDT looking for the MADT (ACPI APIC table). */
    uint64_t sdt_addr = 0;
    int is_xsdt = 0;
    if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
        sdt_addr = rsdp->xsdt_addr;
        is_xsdt = 1;
    } else if (rsdp->rsdt_addr) {
        sdt_addr = rsdp->rsdt_addr;
    }
    if (!sdt_addr)
        return 0;

    struct acpi_sdt_header *sdt = (struct acpi_sdt_header *)phys_to_virt(sdt_addr);
    uint32_t entries = (sdt->length - sizeof(struct acpi_sdt_header)) /
                       (is_xsdt ? 8 : 4);
    uint8_t *tbl = (uint8_t *)sdt + sizeof(struct acpi_sdt_header);

    for (uint32_t i = 0; i < entries; i++) {
        uint64_t taddr = is_xsdt ?
            *(uint64_t *)(tbl + i * 8) :
            (uint64_t)*(uint32_t *)(tbl + i * 4);
        if (!taddr)
            continue;

        struct acpi_sdt_header *h = (struct acpi_sdt_header *)phys_to_virt(taddr);
        if (memcmp(h->signature, "APIC", 4) == 0) {
            acpi_parse_madt(taddr);
            break;
        }
    }
    return g_ioapic_base;
}

int acpi_ioapic_entries(void) {
    return g_ioapic_entries;
}

uint32_t acpi_ioapic_gsi_base(void) {
    return g_ioapic_gsi_base;
}

int acpi_irq_to_gsi(int irq) {
    if (irq < 0 || irq >= 16) return irq;
    return g_irq_to_gsi[irq] ? g_irq_to_gsi[irq] : irq;
}

void acpi_irq_overrides(int irq, int *is_level, int *active_low) {
    if (is_level)    *is_level = (irq < 16 && g_iso_level[irq]);
    if (active_low)  *active_low = (irq < 16 && g_iso_activelow[irq]);
}