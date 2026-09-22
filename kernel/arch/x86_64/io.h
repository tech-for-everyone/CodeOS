#ifndef IO_H
#define IO_H

#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

#endif

/* ── SMAP guards ──
 * stac/clac raise #UD when CR4.SMAP is clear (e.g. qemu64 CPU model), so
 * every use must be conditional.  Without SMAP the access is permitted
 * anyway, so skipping the fence is semantically correct. */
static inline int cpu_smap_enabled(void) {
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    return (int)((cr4 >> 21) & 1ULL);
}
static inline void smap_stac(void) {
    if (cpu_smap_enabled()) __asm__ volatile("stac" : : : "memory");
}
static inline void smap_clac(void) {
    if (cpu_smap_enabled()) __asm__ volatile("clac" : : : "memory");
}
