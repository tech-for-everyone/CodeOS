#ifndef _COMPAT_INTTYPES_H
#define _COMPAT_INTTYPES_H 1

/* Minimal <inttypes.h> for freestanding C builds. intN_t / intmax_t and the
   INTn_C macros come from <stdint.h> (GCC provides them). Only the PRI format
   macros are defined here, for code that formats with standard names. */

#include <stdint.h>

#ifndef PRId64
#define PRId64  "lld"
#define PRIi64  "lli"
#define PRIu64  "llu"
#define PRIx64  "llx"
#define PRIX64  "llX"
#endif

#ifndef PRIdMAX
#define PRIdMAX "lld"
#define PRIiMAX "lli"
#define PRIuMAX "llu"
#define PRIxMAX "llx"
#define PRIXMAX "llX"
#endif

#ifndef PRIdPTR
#define PRIdPTR "ld"
#define PRIiPTR "li"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
#define PRIXPTR "lX"
#endif

#ifndef PRId8
#define PRId8  "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRIi8  "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIu8  "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIx8  "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIX8  "X"
#define PRIX16 "X"
#define PRIX32 "X"
#endif

#endif /* _COMPAT_INTTYPES_H */
