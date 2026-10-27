#include <stdint.h>
#include <stddef.h>

#ifndef __ARCH_ARM_H
#define __ARCH_ARM_H

#define ARM_PAGE_SIZE 4096
#define PL011_UART_BASE 0x09000000
#define PL011_DR (*(volatile uint32_t *)(PL011_UART_BASE + 0x00))
#define PL011_FR (*(volatile uint32_t *)(PL011_UART_BASE + 0x18))
#define PL011_FR_TXFF (1U << 5)

static inline void arm_uart_putc(char c) {
   while (PL011_FR & PL011_FR_TXFF) {
      // wait
   }// while
   PL011_DR = (uint32_t)c;
}// arm_uart_putc

static inline void arm_disable_interrupts(void) {
   asm volatile("cpsid i");
}// arm_disable_interrupts

static inline void arm_enable_interrupts(void) {
   asm volatile("cpsie i");
}// arm_enable_interrupts

/* arm trap */
struct trap_frame_arm {
   uint32_t r0;
   uint32_t r1;
   uint32_t r2;
   uint32_t r3;
   uint32_t r4;
   uint32_t r5;
   uint32_t r6;
   uint32_t r7;
   uint32_t r8;
   uint32_t r9;
   uint32_t r10;
   uint32_t r11;
   uint32_t r12;
   uint32_t sp;
   uint32_t lr;
   uint32_t pc;
   uint32_t spsr;
};

#endif
