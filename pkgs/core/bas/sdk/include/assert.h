/* assert.h -- freestanding assert for the bas runtime, CodeOS and Zircon.
 *
 * The generated C includes <assert.h>, which neither CodeOS's
 * kernel/userspace/include nor Zircon's freestd provides.  This is it.
 *
 * `assert` reports through the CodeOS write syscall rather than stdio: the
 * assert path must work before (and independently of) the formatted-output
 * machinery, and on CodeOS stdio is a thin layer over that same syscall.
 */
#ifndef BAS_ASSERT_H
#define BAS_ASSERT_H

#include <stddef.h>

/* BAS_OS_NAME is supplied by the target spec (-DBAS_OS_NAME=...), so the
 * failure message says which platform rejected it. */
#ifndef BAS_OS_NAME
#define BAS_OS_NAME "freestanding"
#endif

void bas_assert_fail(const char *expr, const char *file, int line,
                     const char *func) __attribute__((noreturn));

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr) \
    ((expr) ? (void)0 : bas_assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

/* `static_assert` is spelled `_Static_assert` in C11; bas emits assert! only,
 * but the alias costs nothing and keeps the header usable from hand-written
 * SDK glue. */
#define static_assert _Static_assert

#endif /* BAS_ASSERT_H */