/* bas_libc.c -- the language-runtime gaps between bas's generated C and the
 * CodeOS and Zircon freestanding C libraries.
 *
 * WHAT IS STILL MISSING HERE, AND WHY
 *
 * The generated C includes <assert.h> and <math.h>, which neither
 * kernel/userspace/include nor Zircon's freestd provides; those headers are in
 * sdk/include.  Everything else the runtime calls -- printf, exit, malloc,
 * free, memcpy, strlen, vsnprintf -- is declared and defined by all three
 * libcs bas targets (glibc, CodeOS, Zircon).
 *
 * That is a much smaller gap than it first looks, and it was reduced on
 * purpose.  The runtime originally failed through
 * `fprintf(stderr, ...); abort();`, and neither symbol survives both targets:
 * CodeOS has no `stderr` and no `fprintf` (so it would not compile), and
 * *neither* CodeOS nor Zircon defines `abort` -- Zircon's freestd even declares
 * it, so the failure there would have been at link.  The codegen now routes
 * every failure through a `bas_panic()` built on printf+exit instead, which is
 * why this file no longer defines either.
 *
 * realloc is the other one, and it is here rather than emitted because it is
 * the only piece of the runtime that has to know how the platform allocates.
 * The emitted runtime calls `bas_realloc(p, old_size, new_size)`, passing the
 * size it already knows, because CodeOS's allocator is a bump allocator:
 * `malloc` keeps no per-block size and `free` is a no-op, so a libc-style
 * realloc(p, n) could not know how much of `p` it was required to preserve.
 */
#include <stdint.h>
#include <stddef.h>

#include "assert.h"

/* The platform allocator, from whichever libc the target spec put on the
 * include path.  Both declare these; CodeOS's malloc takes an int and Zircon's
 * a size_t, and the implicit conversion is exact for any real allocation. */
#include <stdlib.h>
#include <string.h>

/* Raw CodeOS console write, identical on both operating systems.
 *
 * SYSCALL_WRITE is 0 in include/codeos/syscall_abi.h; the number is repeated
 * rather than included because <unistd.h> names a different file in each tree.
 * Going through the syscall instead of stdio is what lets the assert path work
 * before the formatted-output machinery, and independently of it.
 */
#define BAS_SYSCALL_WRITE 0

static void bas_raw(const char *s) {
    unsigned long n = 0;
    if (!s) return;
    while (s[n]) n++;
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(BAS_SYSCALL_WRITE), "D"(s), "S"(n)
                     : "memory");
    (void)ret;
}

static void bas_raw_char(char c) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(BAS_SYSCALL_WRITE), "D"(&c), "S"(1)
                     : "memory");
    (void)ret;
}

/* ── realloc that knows the old size ──
 * Growth only, because every bas grow site increases capacity.  A shrink
 * therefore never reaches this, and returning `p` there is not a shortcut that
 * drops data -- it is the unreachable branch. */
void *bas_realloc(void *p, unsigned long old_size, unsigned long new_size) {
    if (new_size <= old_size) return p;
    void *np = malloc((int)new_size);
    if (!np) return 0;
    if (p && old_size) memcpy(np, p, old_size);
    free(p); /* reclaims on a real heap, no-ops on CodeOS's bump allocator */
    return np;
}

/* ── assert plumbing ──
 * The line number is rendered by hand rather than through snprintf: an assert
 * failure has to be printable even when the formatted-output machinery is what
 * is broken. */

void bas_assert_fail(const char *expr, const char *file, int line,
                     const char *func) __attribute__((noreturn));
void bas_assert_fail(const char *expr, const char *file, int line, const char *func) {
    bas_raw("bas: assert failed: ");
    bas_raw(expr ? expr : "?");
    bas_raw("  at ");
    bas_raw(file ? file : "?");
    bas_raw(":");
    char buf[12];
    int n = 0;
    /* INT_MIN has no positive counterpart, so the magnitude is built as
     * unsigned and the sign prepended by hand. */
    unsigned int v = (line < 0) ? (unsigned int)(-(long)line) : (unsigned int)line;
    if (v == 0) buf[n++] = '0';
    while (v > 0 && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (line < 0 && n < (int)sizeof(buf)) {
        for (int i = n - 1; i >= 0; i--) bas_raw_char(buf[i]);
        bas_raw_char('-');
    } else {
        for (int i = n - 1; i >= 0; i--) bas_raw_char(buf[i]);
    }
    if (func && func[0]) { bas_raw(" in "); bas_raw(func); }
    bas_raw("\n");
    for (;;) { }
}