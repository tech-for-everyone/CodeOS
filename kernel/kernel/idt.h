#ifndef IDT_H
#define IDT_H

#include "types.h"

/* NOTE: Must match push order in vectors.S isr_common:
 *   push r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
 *   (reverse of field order, so rax ends up at the lowest address / offset 0). */
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vec;
    uint64_t err;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) int_frame_t;

void idt_init(void);
void idt_load(void);
void idt_enable(void);
void idt_disable(void);
int  idt_enabled(void);

typedef void (*irq_handler_t)(int_frame_t *);
void irq_register(int irq, irq_handler_t fn);

void isr_set_handler(int vec, void (*fn)(int_frame_t *));

/* default exception handlers */
void isr_divide_error(int_frame_t *);
void isr_debug(int_frame_t *);
void isr_nmi(int_frame_t *);
void isr_breakpoint(int_frame_t *);
void isr_overflow(int_frame_t *);
void isr_bound_range(int_frame_t *);
void isr_invalid_opcode(int_frame_t *);
void isr_device_na(int_frame_t *);
void isr_double_fault(int_frame_t *);
void isr_cso(int_frame_t *);
void isr_invalid_tss(int_frame_t *);
void isr_segment_np(int_frame_t *);
void isr_ssf(int_frame_t *);
void isr_gpf(int_frame_t *);
void isr_page_fault(int_frame_t *);
void isr_fpu(int_frame_t *);
void isr_align_check(int_frame_t *);
void isr_machine_check(int_frame_t *);
void isr_simd(int_frame_t *);
void isr_virt(int_frame_t *);

void irq_handler(int_frame_t *);

#endif
