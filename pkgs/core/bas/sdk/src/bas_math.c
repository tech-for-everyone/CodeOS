/* bas_math.c -- the integer-only <math.h> for bas programs on CodeOS and
 * Zircon, split out from bas_libc.c because the pieces answer to different
 * reasons: this file is math, bas_libc.c is language-runtime plumbing.
 *
 * WHY EVERYTHING HERE IS INTEGER
 *
 * Both targets compile userspace with `-mno-sse -mno-sse2`, which removes the
 * x86-64 float ABI.  clang rejects a floating-point parameter or return under
 * those flags outright ("SSE register return with SSE disabled"), so a float
 * variant of any of these functions could not be *called*, let alone correct.
 * See sdk/include/math.h for the full reasoning.
 */
#include <stdint.h>
#include <stddef.h>

#include "math.h"

long bas_clamp(long v, long lo, long hi) {
    if (lo > hi) { long t = lo; lo = hi; hi = t; }
    return v < lo ? lo : (v > hi ? hi : v);
}

int bas_imin(int a, int b) { return a < b ? a : b; }
int bas_imax(int a, int b) { return a > b ? a : b; }

/* Restoring digit-by-digit square root: two bits of `root` per iteration,
 * built from the most significant end.
 *
 * Chosen over Newton iteration for two reasons that both showed up in practice:
 * a Newton loop needs a seed (the first guess was 128x too large for 32-bit
 * inputs, which truncates x = mag/seed to zero and degenerates into halving),
 * and every Newton exit condition is a chance to stop early on a partially
 * built root.  This version has no seed and cannot stop early: `rem` is the
 * exact running remainder, so the loop always runs its full width.
 */
uint32_t bas_isqrt32(uint32_t v) {
    uint64_t rem = v;
    uint64_t root = 0;
    for (int i = 0; i < 16; i++) {
        root <<= 1;
        uint64_t trial = (root << 1) | 1;        /* 2*root + 1 */
        uint64_t bit = (uint64_t)1 << (30 - 2 * i);
        if (rem >= trial * bit) {
            rem -= trial * bit;
            root |= 1;
        }
    }
    return (uint32_t)root;
}

uint64_t bas_isqrt64(uint64_t v) {
    uint64_t rem = v;
    uint64_t root = 0;
    for (int i = 0; i < 32; i++) {
        root <<= 1;
        uint64_t trial = (root << 1) | 1;
        /* The bit under test is at 62 - 2*i; at i == 31 that is 0, so the
         * guard is not decorative. */
        int shift = 62 - 2 * i;
        uint64_t bit = shift >= 0 ? ((uint64_t)1 << shift) : 0;
        if (rem >= trial * bit) {
            rem -= trial * bit;
            root |= 1;
        }
    }
    return root;
}

int64_t bas_ipow(int64_t base, int exp) {
    int64_t r = 1;
    int neg = exp < 0;
    if (neg) exp = -exp;
    while (exp > 0) {
        if (exp & 1) r *= base;
        base *= base;
        exp >>= 1;
    }
    return neg ? (r ? 1 / r : 0) : r;
}

uint64_t bas_gcd64(uint64_t a, uint64_t b) {
    while (b) { uint64_t t = a % b; a = b; b = t; }
    return a;
}

uint32_t bas_popcount32(uint32_t v) {
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    v = (v + (v >> 4)) & 0x0F0F0F0Fu;
    return (v * 0x01010101u) >> 24;
}

uint32_t bas_clz32(uint32_t v) {
    uint32_t n = 0;
    if (!(v & 0xFFFF0000u)) { n += 16; v <<= 16; }
    if (!(v & 0xFF000000u)) { n += 8;  v <<= 8;  }
    if (!(v & 0xF0000000u)) { n += 4;  v <<= 4;  }
    if (!(v & 0xC0000000u)) { n += 2;  v <<= 2;  }
    if (!(v & 0x80000000u)) { n += 1; }
    return n;
}

uint32_t bas_ctz32(uint32_t v) {
    uint32_t n = 0;
    if (!(v & 0x0000FFFFu)) { n += 16; v >>= 16; }
    if (!(v & 0x000000FFu)) { n += 8;  v >>= 8;  }
    if (!(v & 0x0000000Fu)) { n += 4;  v >>= 4;  }
    if (!(v & 0x00000003u)) { n += 2;  v >>= 2;  }
    if (!(v & 0x00000001u)) { n += 1; }
    return n;
}

/* Deterministic RNG.  Numerical Recipes' LCG rather than a syscall: identical
 * on both OSes and across runs, so a bas program's output does not depend on
 * which target built it -- which is what makes the result testable. */
static uint32_t bas_rng_state = 0x2545F491u;

void bas_srand(uint32_t seed) { bas_rng_state = seed ? seed : 0x2545F491u; }

uint32_t bas_rand(void) {
    bas_rng_state = bas_rng_state * 1664525u + 1013904223u;
    return bas_rng_state;
}

int bas_rand_range(int lo, int hi) {
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    if (span == 0) return (int)bas_rand();  /* full 2^32 range: take it raw */
    return lo + (int)(bas_rand() % span);
}