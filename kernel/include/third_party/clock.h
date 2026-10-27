#ifndef __CLOCK_H
#define __CLOCK_H

#define INTERVAL 10000000
#define CLINT_BASE 0x02000000L
#define CLINT_MTIMECMP_LOW ((volatile uint32_t *)(CLINT_BASE + 0x4000))
#define CLINT_MTIMECMP_HIGH ((volatile uint32_t *)(CLINT_BASE + 0x4004))
#define CLINT_MTIME_LOW ((volatile uint32_t *)(CLINT_BASE + 0xBFF8))
#define CLINT_MTIME_HIGH ((volatile uint32_t *)(CLINT_BASE + 0xBFFC))

#if defined(__RISCV__) || defined(__riscv)
#define set_csrs(reg, bit) asm volatile("csrs " #reg ", %0" :: "r"(bit))
#else
#define set_csrs(reg, bit) ((void)0)
#endif

#endif
