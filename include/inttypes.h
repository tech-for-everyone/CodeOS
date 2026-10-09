/*
 * include/inttypes.h - freestanding <inttypes.h> for the CodeOS kernel.
 *
 * GCC's freestanding x86_64-elf include tree had no <inttypes.h>, so LVGL's
 *   #if __has_include(<inttypes.h>)
 * was false and it fell back to literal "d"/"u"/"x".  Clang DOES ship an
 * <inttypes.h>, but it is only a wrapper whose body is
 *   #include_next <inttypes.h>
 * so with -nostdinc (we do not want the host glibc headers in the kernel)
 * it fails with "fatal error: 'inttypes.h' file not found".
 *
 * This file is what that #include_next now lands on: the standard PRI/SCN
 * format macros for the LP64 target, which is all LVGL (and any other
 * kernel user) needs.  It must stay freestanding - no libc headers.
 */
#ifndef _CODEOS_INTTYPES_H
#define _CODEOS_INTTYPES_H

#include <stdint.h>

/* int32_t / uint32_t */
#define PRId32      "d"
#define PRIi32      "i"
#define PRIo32      "o"
#define PRIu32      "u"
#define PRIx32      "x"
#define PRIX32      "X"
#define SCNd32      "d"
#define SCNi32      "i"
#define SCNo32      "o"
#define SCNu32      "u"
#define SCNx32      "x"

/* int64_t / uint64_t - long on LP64 */
#define PRId64      "ld"
#define PRIi64      "li"
#define PRIo64      "lo"
#define PRIu64      "lu"
#define PRIx64      "lx"
#define PRIX64      "lX"
#define SCNd64      "ld"
#define SCNi64      "li"
#define SCNo64      "lo"
#define SCNu64      "lu"
#define SCNx64      "lx"

/* int8_t / int16_t and the least/fast aliases are the same widths here. */
#define PRId8       "d"
#define PRIi8       "i"
#define PRIo8       "o"
#define PRIu8       "u"
#define PRIx8       "x"
#define PRIX8       "X"
#define PRId16      "d"
#define PRIi16      "i"
#define PRIo16      "o"
#define PRIu16      "u"
#define PRIx16      "x"
#define PRIX16      "X"

#define PRIdLEAST8   PRId8
#define PRIiLEAST8   PRIi8
#define PRIoLEAST8   PRIo8
#define PRIuLEAST8   PRIu8
#define PRIxLEAST8   PRIx8
#define PRIXLEAST8   PRIX8
#define PRIdLEAST16  PRId16
#define PRIiLEAST16  PRIi16
#define PRIoLEAST16  PRIo16
#define PRIuLEAST16  PRIu16
#define PRIxLEAST16  PRIx16
#define PRIXLEAST16  PRIX16
#define PRIdLEAST32  PRId32
#define PRIiLEAST32  PRIi32
#define PRIoLEAST32  PRIo32
#define PRIuLEAST32  PRIu32
#define PRIxLEAST32  PRIx32
#define PRIXLEAST32  PRIX32
#define PRIdLEAST64  PRId64
#define PRIiLEAST64  PRIi64
#define PRIoLEAST64  PRIo64
#define PRIuLEAST64  PRIu64
#define PRIxLEAST64  PRIx64
#define PRIXLEAST64  PRIX64

#define PRIdFAST8    PRId8
#define PRIiFAST8    PRIi8
#define PRIoFAST8    PRIo8
#define PRIuFAST8    PRIu8
#define PRIxFAST8    PRIx8
#define PRIXFAST8    PRIX8
#define PRIdFAST16   PRId16
#define PRIiFAST16   PRIi16
#define PRIoFAST16   PRIo16
#define PRIuFAST16   PRIu16
#define PRIxFAST16   PRIx16
#define PRIXFAST16   PRIX16
#define PRIdFAST32   PRId32
#define PRIiFAST32   PRIi32
#define PRIoFAST32   PRIo32
#define PRIuFAST32   PRIu32
#define PRIxFAST32   PRIx32
#define PRIXFAST32   PRIX32
#define PRIdFAST64   PRId64
#define PRIiFAST64   PRIi64
#define PRIoFAST64   PRIo64
#define PRIuFAST64   PRIu64
#define PRIxFAST64   PRIx64
#define PRIXFAST64   PRIX64

/* intmax_t / uintptr_t are long on LP64. */
#define PRIdMAX     "ld"
#define PRIiMAX     "li"
#define PRIoMAX     "lo"
#define PRIuMAX     "lu"
#define PRIxMAX     "lx"
#define PRIXMAX     "lX"
#define SCNdMAX     "ld"
#define SCNiMAX     "li"
#define SCNoMAX     "lo"
#define SCNuMAX     "lu"
#define SCNxMAX     "lx"

#define PRIdPTR     "ld"
#define PRIiPTR     "li"
#define PRIoPTR     "lo"
#define PRIuPTR     "lu"
#define PRIxPTR     "lx"
#define PRIXPTR     "lX"
#define SCNdPTR     "ld"
#define SCNiPTR     "li"
#define SCNoPTR     "lo"
#define SCNuPTR     "lu"
#define SCNxPTR     "lx"

#endif /* _CODEOS_INTTYPES_H */
