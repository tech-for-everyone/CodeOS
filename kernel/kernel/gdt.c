#include "gdt.h"
#include "idt.h"
#include "kprintf.h"
#include "pmm.h"
#include "string.h"
#include "vmm.h"

#ifdef __aarch64__
/* ARM64 has no GDT/TSS. Syscall setup is done via VBAR_EL1 and SVC #0. */
void gdt_tss_init(void) {}
void gdt_set_tss(uint64_t base, uint32_t limit) { (void)base; (void)limit; kprintf("arm64: stub: gdt_set_tss called\n"); }
void gdt_set_rsp0(uint64_t rsp0) { (void)rsp0; kprintf("arm64: stub: gdt_set_rsp0 called\n"); }
void syscall_msr_init(void *handler_entry) { (void)handler_entry; kprintf("arm64: stub: syscall_msr_init called\n"); }

#else /* x86 */
extern uint64_t gdt64[];
extern uint16_t gdt64_ptr_end[];

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss *tss_entry;

void gdt_set_tss(uint64_t base, uint32_t limit) {
    uint16_t *gdt = (uint16_t *)gdt64;
    uint64_t desc_low, desc_high;

    desc_low  = (limit & 0xFFFF) << 0;
    desc_low |= (base & 0xFFFFFF) << 16;
    desc_low |= (uint64_t)(0x89) << 40;
    desc_low |= (uint64_t)((limit >> 16) & 0x0F) << 48;
    desc_low |= (uint64_t)(0x40) << 48;
    desc_low |= (uint64_t)((base >> 24) & 0xFF) << 56;

    desc_high = (base >> 32) & 0xFFFFFFFF;

    gdt[GDT_TSS / 2]     = (uint16_t)(desc_low >> 0);
    gdt[GDT_TSS / 2 + 1] = (uint16_t)(desc_low >> 16);
    gdt[GDT_TSS / 2 + 2] = (uint16_t)(desc_low >> 32);
    gdt[GDT_TSS / 2 + 3] = (uint16_t)(desc_low >> 48);
    gdt[GDT_TSS / 2 + 4] = (uint16_t)(desc_high >> 0);
    gdt[GDT_TSS / 2 + 5] = (uint16_t)(desc_high >> 16);
    gdt[GDT_TSS / 2 + 6] = (uint16_t)(desc_high >> 32);
    gdt[GDT_TSS / 2 + 7] = (uint16_t)(desc_high >> 48);
}

void gdt_set_rsp0(uint64_t rsp0) {
    if (tss_entry) {
        int smap_on = 0;
        uint64_t cr4;
        __asm__("mov %%cr4, %0" : "=r"(cr4));
        if (cr4 & (1ULL << 21)) smap_on = 1;
        if (smap_on) __asm__ volatile("stac");
        tss_entry->rsp[0] = rsp0;
        if (smap_on) __asm__ volatile("clac");
    }
}

void gdt_tss_init(void) {
    uint64_t tss_phys = pmm_alloc_page();
    tss_entry = (struct tss *)phys_to_virt(tss_phys);
    memset(tss_entry, 0, 4096);

    tss_entry->rsp[0] = 0;
    /* IST1: dedicated stack for IRQ14/15 (IDE). CPU interrupt delivery pushes
     * SS/RSP/RFLAGS/CS/RIP below the interrupted RSP; on the current-stack
     * model that clobbers any red-zone live data. IDE handlers don't use the
     * yield-from-IRQ thread-frame trick, so an IST stack is safe and prevents
     * delivery-time corruption of thread stacks (the proven RIP=0 null-ret
     * cause). 8KB is ample for the exception frame + ATA handler. */
    {
        uint64_t ist1 = 0, prev = 0;
        int contiguous = 1;
        for (int i = 0; i < 2; i++) {
            uint64_t p = pmm_alloc_page();
            if (i == 0) ist1 = p;
            else if (p != prev + 4096) contiguous = 0;
            prev = p;
            vmm_map_page(phys_to_virt(p), p, PAGE_PRESENT | PAGE_WRITE);
        }
        /* Use a contiguous virtual top if pages happened to be contiguous;
         * otherwise the two pages still back the mapped range in practice is
         * not guaranteed — fall back to a single-page top (sufficient). */
        if (contiguous && ist1)
            tss_entry->ist[0] = phys_to_virt(ist1) + 8192;
        else if (ist1)
            tss_entry->ist[0] = phys_to_virt(ist1) + 4096;
    }
    tss_entry->iomap_base = sizeof(struct tss);

    gdt_set_tss((uint64_t)tss_entry, sizeof(struct tss) - 1);

    /* Map TSS page as user-accessible so the CPU can read RSP0 during
       interrupt handling from ring 3. The 2MB huge page in the identity
       mapping has no USER bit, so we split it and set USER explicitly. */
    vmm_map_page((uint64_t)tss_entry, tss_phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    uint16_t tss_sel = GDT_TSS;
    __asm__ volatile("ltr %0" : : "r"(tss_sel));

    /* All hardware IRQs (vectors 32-47) → IST1. CPU delivery pushes 5 qwords
     * below the interrupted RSP, clobbering the 128-byte SysV red zone —
     * prebuilt components (e.g. Qt libs) may use it. isr_common relocates the
     * full interrupt frame from IST to [interrupted_rsp - 128 - 176] on the
     * thread stack (below the red zone) so the red zone is preserved and the
     * timer's yield-from-IRQ / ctx_switch path still operates on the thread
     * stack. */
    for (int v = 32; v <= 47; v++)
        idt_set_ist(v, 1);

    kprintf("GDT/TSS: initialized (tss=0x%lx ist1=%lx irq32-47->IST1)\n",
            (uint64_t)tss_entry, tss_entry->ist[0]);
}

void syscall_msr_init(void *handler_entry) {
    uint64_t star = 0;
    star |= (uint64_t)GDT_KERNEL_CS << 32;
    star |= (uint64_t)GDT_USER_CS << 48;

    uint64_t lstar = (uint64_t)handler_entry;
    uint64_t fmask = 0;

    __asm__ volatile("wrmsr"
        :
        : "c"(0xC0000081), "a"((uint32_t)star), "d"((uint32_t)(star >> 32))
    );
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000082), "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32))
    );
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(0xC0000084), "a"((uint32_t)fmask), "d"((uint32_t)(fmask >> 32))
    );

    uint32_t efer_lo, efer_hi;
    __asm__ volatile("rdmsr" : "=a"(efer_lo), "=d"(efer_hi) : "c"(0xC0000080));
    efer_lo |= 1;
    __asm__ volatile("wrmsr" : : "c"(0xC0000080), "a"(efer_lo), "d"(efer_hi));

    kprintf("GDT: syscall MSRs configured\n");
}
#endif
