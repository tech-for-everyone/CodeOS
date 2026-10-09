#ifndef IO_H
#define IO_H

#include "types.h"

/* ARM64: Memory-mapped IO */
static inline void mmio_write32(uint64_t addr, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    *p = val;
}

static inline uint32_t mmio_read32(uint64_t addr) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    return *p;
}

static inline void mmio_write8(uint64_t addr, uint8_t val) {
    volatile uint8_t *p = (volatile uint8_t *)addr;
    *p = val;
}

static inline uint8_t mmio_read8(uint64_t addr) {
    volatile uint8_t *p = (volatile uint8_t *)addr;
    return *p;
}

/* Legacy x86 IO port compat - not used on ARM64 */
#define outb(port, val) ((void)0)
#define outw(port, val) ((void)0)
#define inb(port) 0
#define inw(port) 0

/* Barrier */
static inline void dmb(void) {
    __asm__ volatile("dmb sy" ::: "memory");
}

#endif
