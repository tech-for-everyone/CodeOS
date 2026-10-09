#include "idt.h"
#include "sched.h"
#include "kprintf.h"
#include "spinlock.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"

#ifdef __aarch64__
/* ARM64: interrupt control is done via DAIF and GIC */
void idt_init(void) {}
void idt_enable(void)  { __asm__ volatile("msr daifclr, #2" : : : "memory"); }
void idt_disable(void) { __asm__ volatile("msr daifset, #2" : : : "memory"); }
int  idt_enabled(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    return (daif & 2) ? 0 : 1;
}
void isr_set_handler(int vec, void (*fn)(int_frame_t *)) { (void)vec; (void)fn; kprintf("arm64: stub: isr_set_handler called\n"); }
void irq_register(int irq, irq_handler_t fn) { (void)irq; (void)fn; kprintf("arm64: stub: irq_register called\n"); }
void isr_dispatch_c(int_frame_t *frame) { (void)frame; kprintf("arm64: stub: isr_dispatch_c called\n"); }

#else /* x86 */
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/apic.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define IDT_FLAG_PRESENT  (1 << 15)
#define IDT_FLAG_INT_GATE (0xE << 8)
#define IDT_FLAG_DPL3     (3 << 13)
#define IDT_FLAG_DPL0     (0 << 13)

typedef struct {
    uint16_t offset_lo;
    uint16_t selector;
    uint16_t flags;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256] __attribute__((aligned(16)));
static irq_handler_t irq_handlers[16];
static void (*custom_handlers[256])(int_frame_t *);

extern uint64_t vector_table[256];

static void idt_set(int vec, uint16_t sel, uint16_t flags) {
    uint64_t addr = vector_table[vec];
    idt[vec].offset_lo  = addr & 0xFFFF;
    idt[vec].selector   = sel;
    idt[vec].flags      = flags;
    idt[vec].offset_mid = (addr >> 16) & 0xFFFF;
    idt[vec].offset_hi  = (addr >> 32) & 0xFFFFFFFF;
    idt[vec].reserved   = 0;
}

void isr_set_handler(int vec, void (*fn)(int_frame_t *)) {
    custom_handlers[vec] = fn;
}

void irq_register(int irq, irq_handler_t fn) {
    if (irq < 0 || irq >= 16) return;
    irq_handlers[irq] = fn;
    outb(irq < 8 ? PIC1_DATA : PIC2_DATA,
         inb(irq < 8 ? PIC1_DATA : PIC2_DATA) & ~(1 << (irq & 7)));
}

void idt_enable(void)  { __asm__ volatile("sti"); }
void idt_disable(void) { __asm__ volatile("cli"); }
int  idt_enabled(void) {
    uint64_t f;
    __asm__ volatile("pushf; pop %0" : "=r"(f));
    return (f >> 9) & 1;
}

static const char *exc_name[32] = {
    "DE","DB","NMI","BP","OF","BR","UD","NM","DF","CSO","TS","NP",
    "SS","GP","PF","-","MF","AC","MC","XM","VE","-","-","-","-","-","-","-","-","-","SX","-"
};

extern volatile int need_resched;
extern void sched_yield(void);

void isr_dispatch_c(int_frame_t *frame) {
    int vec = frame->vec;

    if (vec >= 0x20 && vec < 0x30) {
        int irq = vec - 0x20;
        if (irq_handlers[irq]) irq_handlers[irq](frame);
        if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
        outb(PIC1_CMD, PIC_EOI);
        apic_eoi();

        if (need_resched && vec == 0x20) {
            need_resched = 0;
            __asm__ volatile("cli" : : : "memory");
            sched_yield();
        }
        return;
    }

    if (custom_handlers[vec]) {
        custom_handlers[vec](frame);
        return;
    }

    if (vec < 32) {
        if (vec == 14) {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            uint64_t err = frame->err;

            /* Check for COW page fault: write to present read-only page with COW flag */
            if ((err & 0x3) == 0x3) {
                uint64_t flags;
                if (vmm_get_mapping(cr2, 0, &flags) == 0 && (flags & PAGE_COW)) {
                    uint64_t new_phys = (uint64_t)pmm_alloc_page();
                    if (new_phys) {
                        uint64_t old_phys;
                        vmm_get_mapping(cr2, &old_phys, 0);
                        memcpy((void*)phys_to_virt(new_phys),
                               (void*)phys_to_virt(old_phys), 0x1000);
                        uint64_t new_flags = (flags | PAGE_WRITE) & ~PAGE_COW;
                        /* Don't free old_phys — the other process still has it mapped as COW */
                        vmm_unmap_page(cr2);
                        vmm_map_page(cr2, new_phys, new_flags);
                        return;
                    }
                }
            }

            /* User-mode fault on a 2MB/1GB huge page that lacks PAGE_USER:
               split it so the specific 4KB page gets user permission. */
            if ((err & 7) == 5 || (err & 7) == 7) {
                uint64_t flags;
                if (vmm_get_mapping(cr2, 0, &flags) == 0 &&
                    (flags & PAGE_HUGE) && !(flags & PAGE_USER)) {
                    uint64_t phys;
                    vmm_get_mapping(cr2, &phys, 0);
                    vmm_map_page(cr2, phys,
                        (flags | PAGE_USER) & ~PAGE_COW);
                    return;
                }
            }

            kprintf("\n!!! PF at RIP=0x%lx CR2=0x%lx err=%lu !!!\n",
                    frame->rip, cr2, err);
            kprintf("cs=0x%lx rfl=0x%lx hw_rsp=0x%lx\n",
                    frame->cs, frame->rflags, frame->rsp);
            kprintf("regs: rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx\n",
                    frame->rax, frame->rbx, frame->rcx, frame->rdx);
            kprintf("      rsi=0x%lx rdi=0x%lx rbp=0x%lx r8=0x%lx\n",
                    frame->rsi, frame->rdi, frame->rbp, frame->r8);
            kprintf("      r9=0x%lx r10=0x%lx r11=0x%lx r12=0x%lx\n",
                    frame->r9, frame->r10, frame->r11, frame->r12);
            kprintf("      r13=0x%lx r14=0x%lx r15=0x%lx\n",
                    frame->r13, frame->r14, frame->r15);
            thread_t *cur = sched_current();
            if (cur)
                kprintf("      cur=%s sysstack=[%lx,%lx) rsp=%lx\n",
                        cur->name,
                        (uint64_t)cur->syscall_stack_top - 16384UL,
                        (uint64_t)cur->syscall_stack_top, frame->rsp);
        } else {
            kprintf("\n!!! %s (vec %d) at RIP=0x%lx CS=0x%lx RSP=0x%lx err=%lu !!!\n",
                    vec < 32 ? exc_name[vec] : "???", vec,
                    frame->rip, frame->cs, frame->rsp, frame->err);
            kprintf("regs: rax=0x%lx rbx=0x%lx rcx=0x%lx rdx=0x%lx\n",
                    frame->rax, frame->rbx, frame->rcx, frame->rdx);
            kprintf("      rsi=0x%lx rdi=0x%lx rbp=0x%lx r8=0x%lx\n",
                    frame->rsi, frame->rdi, frame->rbp, frame->r8);
            kprintf("      r9=0x%lx r10=0x%lx r11=0x%lx r12=0x%lx\n",
                    frame->r9, frame->r10, frame->r11, frame->r12);
            kprintf("      r13=0x%lx r14=0x%lx r15=0x%lx\n",
                    frame->r13, frame->r14, frame->r15);
            thread_t *cur = sched_current();
            if (cur)
kprintf("      cur=%s sysstack=[%lx,%lx) rsp=%lx\n",
                    cur->name,
                    (uint64_t)cur->syscall_stack_top - 16384UL,
                    (uint64_t)cur->syscall_stack_top, frame->rsp);
        }
        for (;;) __asm__ volatile("cli; hlt");
    }
}

static void pic_remap(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0x00); io_wait();
    outb(PIC2_DATA, 0x00); io_wait();
}

void idt_init(void) {
    uint16_t cs;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));

    for (int i = 0; i < 256; i++) {
        uint16_t flags = IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL0;
        if (i == 3 || i == 0x80) flags = IDT_FLAG_PRESENT | IDT_FLAG_INT_GATE | IDT_FLAG_DPL3;
        idt_set(i, cs, flags);
    }

    pic_remap();

    idt_ptr_t ptr;
    ptr.limit = sizeof(idt) - 1;
    ptr.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(ptr));
}
#endif
