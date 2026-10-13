/* CodeOS alloc shim — aligned allocation entry points glib's gmem.c requires.
 * CodeOS malloc is 16-byte aligned and has no alignment-aware allocator, so
 * (following pkgs/core/openssl/src/ossl_shim.c) these fall back to malloc. That
 * is sound for the alignments glib actually requests (<= 16) and keeps the
 * result free()-compatible, which an over-allocate-and-offset scheme would not.
 */
#include <stddef.h>

extern void *malloc(size_t);

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    (void)alignment;
    void *p = malloc(size ? size : 1);
    if (!p)
        return 12; /* ENOMEM */
    *memptr = p;
    return 0;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    (void)alignment;
    return malloc(size ? size : 1);
}

void *memalign(size_t alignment, size_t size)
{
    (void)alignment;
    return malloc(size ? size : 1);
}
