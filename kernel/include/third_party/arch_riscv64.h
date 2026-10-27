#include <stdint.h>
#include <stddef.h>

#ifndef __ARCH_RISCV64_H
#define __ARCH_RISCV64_H

#define RV64_PAGE_SIZE 4096
#define RV64_PAGE_SHIFT 12

#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_G (1UL << 5)
#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)

#define SATP_MODE_SV39 (8UL << 60)
#define SATP_MODE_SV48 (9UL << 60)

static inline uint64_t rv64_read_csr_mstatus(void) {
   uint64_t val;
   asm volatile("csrr %0, mstatus" : "=r"(val));
   return val;
}// rv64_read_csr_mstatus

static inline void rv64_write_csr_mstatus(uint64_t val) {
   asm volatile("csrw mstatus, %0" :: "r"(val));
}// rv64_write_csr_mstatus

static inline void rv64_fence_i(void) {
   asm volatile("fence.i");
}// rv64_fence_i

/* rrap riscv64 */
struct trap_frame_rv64 {
   uint64_t mepc;
   uint64_t ra;
   uint64_t sp;
   uint64_t gp;
   uint64_t tp;
   uint64_t t0;
   uint64_t t1;
   uint64_t t2;
   uint64_t s0;
   uint64_t s1;
   uint64_t a0;
   uint64_t a1;
   uint64_t a2;
   uint64_t a3;
   uint64_t a4;
   uint64_t a5;
   uint64_t a6;
   uint64_t a7;
   uint64_t s2;
   uint64_t s3;
   uint64_t s4;
   uint64_t s5;
   uint64_t s6;
   uint64_t s7;
   uint64_t s8;
   uint64_t s9;
   uint64_t s10;
   uint64_t s11;
   uint64_t t3;
   uint64_t t4;
   uint64_t t5;
   uint64_t t6;
   uint64_t mstatus;
   uint64_t mcause;
   uint64_t mtval;
};

#endif
