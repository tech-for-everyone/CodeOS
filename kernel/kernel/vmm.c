#include "vmm.h"
#include "pmm.h"
#include "kprintf.h"
#include "string.h"
#include "spinlock.h"

#define PAGE_TABLES 512
#define PML4_SHIFT  39
#define PDP_SHIFT   30
#define PD_SHIFT    21
#define PT_SHIFT    12

static uint64_t *kernel_pml4;
static spinlock_t vmm_lock = SPINLOCK_INIT;

/* phys_to_virt/virt_to_phys defined in vmm.h using phys_to_virt_base */

static inline void tlb_flush(uint64_t virt) {
#ifdef __aarch64__
    __asm__ volatile("dsb ishst; tlbi vaaae1is, %0; dsb ish; isb" : : "r"(virt >> 12) : "memory");
#else
    __asm__ volatile("invlpg %0" : : "m"(*(volatile char*)virt) : "memory");
#endif
}

static inline uint64_t read_page_table_base(void) {
#ifdef __aarch64__
    uint64_t val;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(val));
    return val;
#else
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
#endif
}

static inline void write_page_table_base(uint64_t val) {
#ifdef __aarch64__
    __asm__ volatile("msr ttbr0_el1, %0; isb" : : "r"(val) : "memory");
#else
    __asm__ volatile("mov %0, %%cr3" : : "r"(val) : "memory");
#endif
}

static inline uint64_t *pml4_entry(uint64_t virt) {
    return &kernel_pml4[(virt >> PML4_SHIFT) & 0x1FF];
}

static inline uint64_t *get_pdp(uint64_t *pml4e) {
    if (!(*pml4e & PAGE_PRESENT)) return 0;
    return (uint64_t*)phys_to_virt(*pml4e & ~0xFFF);
}

static inline uint64_t *pdp_entry(uint64_t virt, uint64_t *pdp) {
    return &pdp[(virt >> PDP_SHIFT) & 0x1FF];
}

static inline uint64_t *get_pd(uint64_t *pdpe) {
    if (!(*pdpe & PAGE_PRESENT)) return 0;
    return (uint64_t*)phys_to_virt(*pdpe & ~0xFFF);
}

static inline uint64_t *pd_entry(uint64_t virt, uint64_t *pd) {
    return &pd[(virt >> PD_SHIFT) & 0x1FF];
}

static inline uint64_t *get_pt(uint64_t *pde) {
    if (!(*pde & PAGE_PRESENT)) return 0;
    return (uint64_t*)phys_to_virt(*pde & ~0xFFF);
}

static inline uint64_t *pt_entry(uint64_t virt, uint64_t *pt) {
    return &pt[(virt >> PT_SHIFT) & 0x1FF];
}

void vmm_init(void) {
    kernel_pml4 = (uint64_t *)phys_to_virt(read_page_table_base());
}

static uint64_t *alloc_table(void) {
    uint64_t phys = (uint64_t)pmm_alloc_page();
    if (!phys) return 0;
    uint64_t *table = (uint64_t*)phys_to_virt(phys);
    memset(table, 0, PAGE_SIZE);
    return table;
}

static uint64_t *ensure_pdp(uint64_t *pml4e, uint64_t flags) {
    uint64_t user_flag = (flags & PAGE_USER);
    if (*pml4e & PAGE_PRESENT) {
        if (user_flag && !(*pml4e & PAGE_USER)) {
            *pml4e |= PAGE_USER;
        }
        *pml4e |= user_flag;
        return get_pdp(pml4e);
    }
    uint64_t *pdp = alloc_table();
    if (!pdp) return 0;
    *pml4e = virt_to_phys((uint64_t)pdp) | PAGE_PRESENT | PAGE_WRITE | user_flag;
    return pdp;
}

static uint64_t *ensure_pd(uint64_t *pdpe, uint64_t flags, uint64_t virt_1gb) {
    uint64_t user_flag = (flags & PAGE_USER);
    if (*pdpe & (1ULL << 7)) {
        uint64_t large_phys = *pdpe & ~((1ULL << 7) | 0xFFF);
        uint64_t *pd = alloc_table();
        if (!pd) return 0;
        for (int i = 0; i < 512; i++)
            pd[i] = (large_phys + (uint64_t)i * 0x200000) | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE | user_flag;
        *pdpe = virt_to_phys((uint64_t)pd) | PAGE_PRESENT | PAGE_WRITE | user_flag;
        tlb_flush(virt_1gb);
        return pd;
    }
    if (*pdpe & PAGE_PRESENT) {
        *pdpe |= user_flag;
        return get_pd(pdpe);
    }
    uint64_t *pd = alloc_table();
    if (!pd) return 0;
    *pdpe = virt_to_phys((uint64_t)pd) | PAGE_PRESENT | PAGE_WRITE | user_flag;
    return pd;
}

static uint64_t *ensure_pt(uint64_t *pde, uint64_t flags, uint64_t virt_2mb) {
    uint64_t user_flag = (flags & PAGE_USER);
    if (!(*pde & PAGE_PRESENT)) {
        uint64_t *pt = alloc_table();
        if (!pt) return 0;
        *pde = virt_to_phys((uint64_t)pt) | PAGE_PRESENT | PAGE_WRITE | user_flag;
        return pt;
    }
    if (*pde & (1ULL << 7)) {
        uint64_t large_phys = *pde & ~((1ULL << 7) | 0xFFF);
        uint64_t *pt = alloc_table();
        if (!pt) return 0;
        for (int i = 0; i < 512; i++)
            pt[i] = (large_phys + i * 0x1000) | PAGE_PRESENT | PAGE_WRITE | user_flag;
        *pde = virt_to_phys((uint64_t)pt) | PAGE_PRESENT | PAGE_WRITE | user_flag;
        tlb_flush(virt_2mb);
        return pt;
    }
    *pde |= user_flag;
    return get_pt(pde);
}

int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    spin_lock(&vmm_lock);

    uint64_t *pml4e = pml4_entry(virt);
    uint64_t *pdp = ensure_pdp(pml4e, flags);
    if (!pdp) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pdpe = pdp_entry(virt, pdp);
    uint64_t *pd = ensure_pd(pdpe, flags, virt & ~0x3FFFFFFFULL);
    if (!pd) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pde = pd_entry(virt, pd);
    uint64_t *pt = ensure_pt(pde, flags, virt & ~0x1FFFFFULL);
    if (!pt) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pte = pt_entry(virt, pt);
    *pte = (phys & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;

    tlb_flush(virt);
    spin_unlock(&vmm_lock);
    return 0;
}

int vmm_unmap_page(uint64_t virt) {
    spin_lock(&vmm_lock);

    uint64_t *pml4e = pml4_entry(virt);
    uint64_t *pdp = get_pdp(pml4e);
    if (!pdp) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pdpe = pdp_entry(virt, pdp);
    uint64_t *pd = get_pd(pdpe);
    if (!pd) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pde = pd_entry(virt, pd);
    if (!(*pde & PAGE_PRESENT)) { spin_unlock(&vmm_lock); return -1; }

    /* Handle 2MB huge page: split into 4KB pages before unmapping */
    if (*pde & (1ULL << 7)) {
        uint64_t large_phys = *pde & ~((1ULL << 7) | 0xFFF);
        uint64_t *pt = alloc_table();
        if (!pt) { spin_unlock(&vmm_lock); return -1; }
        for (int i = 0; i < 512; i++)
            pt[i] = (large_phys + i * 0x1000) | PAGE_PRESENT | PAGE_WRITE;
        *pde = virt_to_phys((uint64_t)pt) | PAGE_PRESENT | PAGE_WRITE;
        tlb_flush(virt & ~0x1FFFFFULL);
    }

    uint64_t *pt = get_pt(pde);
    if (!pt) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pte = pt_entry(virt, pt);
    uint64_t old_entry = *pte;
    *pte = 0;

    if (old_entry) {
        uint64_t old_phys = old_entry & ~0xFFF;
        if (old_entry & PAGE_COW) {
            spin_unlock(&vmm_lock);
            return 0;
        }
        if (pmm_is_page_allocated(old_phys))
            pmm_free_page(old_phys);
    }

    tlb_flush(virt);
    spin_unlock(&vmm_lock);
    return 0;
}

int vmm_get_mapping(uint64_t virt, uint64_t *phys, uint64_t *flags) {
    uint64_t *pml4e = pml4_entry(virt);
    uint64_t *pdp = get_pdp(pml4e);
    if (!pdp) return -1;

    uint64_t *pdpe = pdp_entry(virt, pdp);
    uint64_t *pd = get_pd(pdpe);
    if (!pd) return -1;

    uint64_t *pde = pd_entry(virt, pd);
    if (!(*pde & PAGE_PRESENT)) return -1;

    /* Handle 2MB huge page */
    if (*pde & (1ULL << 7)) {
        if (phys) *phys = (*pde & ~((1ULL << 7) | 0xFFF)) + (virt & 0x1FFFFF);
        if (flags) *flags = *pde & 0xFFF;
        return 0;
    }

    uint64_t *pt = get_pt(pde);
    if (!pt) return -1;

    uint64_t *pte = pt_entry(virt, pt);
    if (!(*pte & PAGE_PRESENT)) return -1;

    if (phys) *phys = *pte & ~0xFFF;
    if (flags) *flags = *pte & 0xFFF;
    return 0;
}

/* Translate a virtual address to physical using the current page tables.
 * Kernel image addresses (0xffffffff80000000+) and HHDM addresses are both
 * resolved through the page tables because loaders like Limine may place the
 * image at an arbitrary physical address. Handles 4KB, 2MB and 1GB pages.
 * Returns 0 on failure. */
uint64_t vmm_virt_to_phys(uint64_t virt) {
    uint64_t *pml4 = (uint64_t *)phys_to_virt(read_page_table_base() & ~0xFFFULL);
    uint64_t pml4e = pml4[(virt >> PML4_SHIFT) & 0x1FF];
    if (!(pml4e & PAGE_PRESENT)) return 0;

    uint64_t *pdpt = (uint64_t *)phys_to_virt(pml4e & ~0xFFFULL);
    uint64_t pdpte = pdpt[(virt >> PDP_SHIFT) & 0x1FF];
    if (!(pdpte & PAGE_PRESENT)) return 0;
    if (pdpte & PAGE_HUGE)
        return (pdpte & 0x000FFFFFC0000000ULL) + (virt & 0x3FFFFFFFULL);

    uint64_t *pd = (uint64_t *)phys_to_virt(pdpte & ~0xFFFULL);
    uint64_t pde = pd[(virt >> PD_SHIFT) & 0x1FF];
    if (!(pde & PAGE_PRESENT)) return 0;
    if (pde & PAGE_HUGE)
        return (pde & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFFULL);

    uint64_t *pt = (uint64_t *)phys_to_virt(pde & ~0xFFFULL);
    uint64_t pte = pt[(virt >> PT_SHIFT) & 0x1FF];
    if (!(pte & PAGE_PRESENT)) return 0;

    return (pte & ~0xFFFULL) + (virt & 0xFFFULL);
}

int vmm_set_flags(uint64_t virt, uint64_t flags) {
    spin_lock(&vmm_lock);

    uint64_t *pml4e = pml4_entry(virt);
    uint64_t *pdp = get_pdp(pml4e);
    if (!pdp) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pdpe = pdp_entry(virt, pdp);
    uint64_t *pd = get_pd(pdpe);
    if (!pd) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pde = pd_entry(virt, pd);
    if (!(*pde & PAGE_PRESENT)) { spin_unlock(&vmm_lock); return -1; }

    if (*pde & (1ULL << 7)) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pt = get_pt(pde);
    if (!pt) { spin_unlock(&vmm_lock); return -1; }

    uint64_t *pte = pt_entry(virt, pt);
    if (!(*pte & PAGE_PRESENT)) { spin_unlock(&vmm_lock); return -1; }

    uint64_t phys = *pte & ~0xFFF;
    *pte = phys | (flags & 0xFFF) | PAGE_PRESENT;

    tlb_flush(virt);
    spin_unlock(&vmm_lock);
    return 0;
}

void vmm_switch_pml4(uint64_t *pml4) {
    write_page_table_base((uint64_t)pml4);
}

uint64_t *vmm_current_pml4(void) {
    return (uint64_t *)read_page_table_base();
}

uint64_t *vmm_get_cr3(void) { return vmm_current_pml4(); }

void vmm_set_cr3(uint64_t *pml4) { vmm_switch_pml4(pml4); }

/* The shared kernel address space — kernel threads must run on this
 * even if a stale user-process CR3 is still loaded. */
uint64_t *vmm_kernel_pml4(void) { return kernel_pml4; }

