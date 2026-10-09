#include "rng.h"
#include "kprintf.h"
#include "../drivers/timer.h"

static uint64_t rng_state;
static int rng_hw_ok;

static int rdrand64(uint64_t *val) {
    unsigned char ok;
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    if (!(ecx & (1 << 30))) return 0;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(*val), "=qm"(ok));
    return ok;
}

static int rdseed64(uint64_t *val) {
    unsigned char ok;
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    if (!(ebx & (1 << 18))) return 0;
    __asm__ volatile("rdseed %0; setc %1" : "=r"(*val), "=qm"(ok));
    return ok;
}

void rng_seed(uint64_t s) {
    rng_state = s ? s : 1;
}

static void rng_stir(uint64_t x) {
    rng_state ^= x;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
}

uint64_t rng_next(void) {
    if (rng_state == 0) {
        uint64_t hw = 0;
        if (rdrand64(&hw)) {
            rng_state = hw;
            rng_hw_ok = 1;
        } else if (rdseed64(&hw)) {
            rng_state = hw;
            rng_hw_ok = 1;
        } else {
            rng_state = timer_get_milliseconds();
            rng_state ^= (uint64_t)(uintptr_t)&rng_state;
            rng_state ^= rng_state << 13;
        }
    }

    if (rng_hw_ok) {
        uint64_t hw = 0;
        if (rdrand64(&hw)) {
            uint64_t x = rng_state;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            rng_state = x;
            return x ^ hw;
        }
    }

    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

void rng_init(void) {
    uint64_t hw = 0;
    rng_hw_ok = rdrand64(&hw) || rdseed64(&hw);
    if (rng_hw_ok) {
        rng_state = hw;
        rng_stir(timer_get_milliseconds());
    } else {
        rng_state = timer_get_milliseconds();
        rng_state ^= (uint64_t)(uintptr_t)&rng_state;
        rng_state ^= rng_state << 13;
    }
}
