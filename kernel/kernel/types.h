#ifndef TYPES_H
#define TYPES_H

/* Freestanding kernel types — no libc dependency.
 * Use compiler builtins so types match the platform exactly. */

#ifdef __GNUC__
#ifndef va_start
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)
#endif
#endif

/* Integer types using compiler builtins for correct platform widths.
 * These match what <stdint.h> defines, so no conflict when external
 * headers (e.g. pixelman.h) include <stdint.h> after us. */
typedef __INT8_TYPE__    int8_t;
typedef __UINT8_TYPE__   uint8_t;
typedef __INT16_TYPE__   int16_t;
typedef __UINT16_TYPE__  uint16_t;
typedef __INT32_TYPE__   int32_t;
typedef __UINT32_TYPE__  uint32_t;
typedef __INT64_TYPE__   int64_t;
typedef __UINT64_TYPE__  uint64_t;

/* Pointer-width types — use compiler builtins so they match stddef.h */
typedef __SIZE_TYPE__    size_t;
typedef signed long      ssize_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

/* Limits — guard against redefinition from <stdint.h> */
#ifndef INT8_MIN
#define INT8_MIN    (-128)
#endif
#ifndef INT8_MAX
#define INT8_MAX    127
#endif
#ifndef UINT8_MAX
#define UINT8_MAX   255
#endif
#ifndef INT16_MIN
#define INT16_MIN   (-32768)
#endif
#ifndef INT16_MAX
#define INT16_MAX   32767
#endif
#ifndef UINT16_MAX
#define UINT16_MAX  65535
#endif
#ifndef INT32_MIN
#define INT32_MIN   (-2147483647-1)
#endif
#ifndef INT32_MAX
#define INT32_MAX   2147483647
#endif
#ifndef UINT32_MAX
#define UINT32_MAX  4294967295U
#endif
#ifndef INT64_MIN
#define INT64_MIN   (-9223372036854775807LL-1)
#endif
#ifndef INT64_MAX
#define INT64_MAX   9223372036854775807LL
#endif
#ifndef UINT64_MAX
#define UINT64_MAX  18446744073709551615ULL
#endif
#ifndef SIZE_MAX
#define SIZE_MAX    UINT64_MAX
#endif

/* Booleans */
#ifndef __cplusplus
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
typedef _Bool bool;
#define true  1
#define false 0
#else
#ifndef true
#define true  1
#endif
#ifndef false
#define false 0
#endif
#endif
#endif

/* NULL — guard against redefinition */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* Compiler hints */
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef unreachable
#define unreachable() __builtin_unreachable()
#endif

#endif
