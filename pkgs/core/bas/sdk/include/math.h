/* math.h -- integer-only math for the bas runtime on CodeOS and Zircon.
 *
 * WHY THIS FILE HAS NO FLOATS
 *
 * Both targets compile userspace with `-mno-sse -mno-sse2` (see
 * kernel/userspace/Makefile's USER_CFLAGS and Zircon's), which removes the
 * x86-64 float ABI entirely.  clang does not degrade gracefully here: passing
 * or returning a `double` under -mno-sse is a hard error
 *
 *     error: SSE register return with SSE disabled
 *
 * so a floating-point bas program cannot be *compiled* for these targets at
 * all.  That is the right failure -- a silently wrong float ABI would produce
 * a binary that builds and then miscomputes -- but it means this header must
 * not offer float functions it cannot honour.  A bas program using `f64` gets
 * clang's error above, which names the real cause.
 *
 * Scope note: the generated C includes <math.h> unconditionally but calls
 * nothing from it, so this header exists to satisfy the include and to give
 * the SDK's own integer helpers a home.  Everything is `bas_`-prefixed so it
 * can never collide with a platform libc symbol.
 */
#ifndef BAS_MATH_H
#define BAS_MATH_H

#include <stdint.h>
#include <stddef.h>

/* ── Integer helpers ── */
long bas_clamp(long v, long lo, long hi);   /* clamp v into [lo, hi] */
int  bas_imin(int a, int b);
int  bas_imax(int a, int b);

/* ── Integer square root ──
 * floor(sqrt(v)), by the restoring digit-by-digit method: no floating point,
 * no Newton seed to get wrong, and no early exit on a partial result. */
uint32_t bas_isqrt32(uint32_t v);
uint64_t bas_isqrt64(uint64_t v);

/* ── Integer arithmetic ── */
int64_t  bas_ipow(int64_t base, int exp);
uint64_t bas_gcd64(uint64_t a, uint64_t b);
uint32_t bas_popcount32(uint32_t v);
uint32_t bas_clz32(uint32_t v);   /* v != 0 */
uint32_t bas_ctz32(uint32_t v);   /* v != 0 */

/* ── Deterministic RNG ──
 * A fixed LCG rather than a syscall: reproducible across CodeOS and Zircon and
 * across runs, which is what a test in a bas program needs.  Seed with
 * bas_srand() for a different sequence. */
void     bas_srand(uint32_t seed);
uint32_t bas_rand(void);
int      bas_rand_range(int lo, int hi); /* inclusive at both ends */

#endif /* BAS_MATH_H */