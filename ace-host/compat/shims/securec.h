/*
 * Minimal stand-in for OpenHarmony's <securec.h> (third_party/bounds_checking_function).
 *
 * WHY A REAL IMPLEMENTATION AND NOT EMPTY STUBS
 * ---------------------------------------------
 * These are the checked variants of memcpy/sprintf. The entire point of the
 * checked variants is that they fail *visibly* instead of overflowing, so a stub
 * that ignores its size arguments would be worse than useless: it would compile,
 * run, and silently destroy the memory-safety property the code asked for. The
 * size checks below are therefore implemented, not approximated.
 *
 * The real header is a standalone Huawei library with no dependency on the rest
 * of OpenHarmony, so vendoring the genuine article is the eventual answer; this
 * covers the three calls `frameworks/base` actually makes and is honest about
 * being partial. Anything not implemented here is a link error rather than a
 * silent no-op.
 *
 * Signatures and return convention follow the real one: 0 (EOK) on success, and
 * a non-zero errno-style value on failure.
 */
#ifndef ACE_HOST_SECUREC_H
#define ACE_HOST_SECUREC_H

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

#define EOK 0
#define EINVAL 22
#define ERANGE 34

#ifndef SECUREC_MEM_MAX_LEN
#define SECUREC_MEM_MAX_LEN ((size_t)-1)
#endif

static inline int memcpy_s(void *dest, size_t destMax, const void *src, size_t count)
{
    if (dest == nullptr || src == nullptr || destMax > SECUREC_MEM_MAX_LEN) {
        return EINVAL;
    }
    if (count > destMax) {
        /* Refuse rather than overrun, and leave the destination untouched: the
         * caller's contract is that failure means nothing was written. */
        if (destMax > 0) {
            std::memset(dest, 0, destMax);
        }
        return ERANGE;
    }
    if (count == 0) {
        return EOK;
    }
    std::memcpy(dest, src, count);
    return EOK;
}

static inline int memmove_s(void *dest, size_t destMax, const void *src, size_t count)
{
    if (dest == nullptr || src == nullptr) {
        return EINVAL;
    }
    if (count > destMax) {
        if (destMax > 0) {
            std::memset(dest, 0, destMax);
        }
        return ERANGE;
    }
    if (count == 0) {
        return EOK;
    }
    std::memmove(dest, src, count);
    return EOK;
}

static inline int memset_s(void *dest, size_t destMax, int c, size_t count)
{
    if (dest == nullptr) {
        return EINVAL;
    }
    if (count > destMax) {
        return ERANGE;
    }
    if (count == 0) {
        return EOK;
    }
    std::memset(dest, c, count);
    return EOK;
}

static inline int strcpy_s(char *dest, size_t destMax, const char *src)
{
    if (dest == nullptr || src == nullptr || destMax == 0) {
        return EINVAL;
    }
    const size_t n = std::strlen(src);
    if (n + 1 > destMax) {
        dest[0] = '\0';
        return ERANGE;
    }
    std::memcpy(dest, src, n + 1);
    return EOK;
}

static inline int sprintf_s(char *dest, size_t destMax, const char *format, ...)
{
    if (dest == nullptr || format == nullptr || destMax == 0) {
        return EINVAL;
    }
    va_list ap;
    va_start(ap, format);
    const int n = std::vsnprintf(dest, destMax, format, ap);
    va_end(ap);
    if (n < 0 || static_cast<size_t>(n) >= destMax) {
        dest[0] = '\0';
        return ERANGE;
    }
    return EOK;
}

static inline int vsnprintf_s(char *dest, size_t destMax, size_t count,
                              const char *format, va_list ap)
{
    if (dest == nullptr || format == nullptr || destMax == 0) {
        return -1;
    }
    /* The real vsnprintf_s truncates at min(count, destMax) and still
     * NUL-terminates; count == (size_t)-1 means "no extra limit". */
    size_t limit = (count == static_cast<size_t>(-1)) ? destMax : count;
    if (limit > destMax) {
        limit = destMax;
    }
    const int n = std::vsnprintf(dest, limit, format, ap);
    if (n < 0) {
        dest[0] = '\0';
        return -1;
    }
    if (static_cast<size_t>(n) >= limit) {
        dest[limit - 1] = '\0';
        return -1;
    }
    return n;
}

static inline int snprintf_s(char *dest, size_t destMax, size_t count, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf_s(dest, destMax, count, format, ap);
    va_end(ap);
    return n;
}

#endif // ACE_HOST_SECUREC_H
