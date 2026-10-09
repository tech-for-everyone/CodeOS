#ifndef _FEATURES_H
#define _FEATURES_H 1
#define __GLIBC__ 2
#define __GLIBC_MINOR__ 40
#define __GLIBC_PREREQ(maj, min) ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= ((maj) << 16) + (min))

#define __GNUC_PREREQ(maj, min) \
  ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#define __glibc_clang_prereq(maj, min) 0
#define __USE_FORTIFY_LEVEL 0
#define _FORTIFY_SOURCE 0

/* Simulate _GNU_SOURCE so glibc exposes all __USE_* macros */
#define _GNU_SOURCE 1
#define __USE_ISOC99 1
#define __USE_ISOC11 1
#define __USE_POSIX 1
#define __USE_POSIX2 1
#define __USE_XOPEN 1
#define __USE_XOPEN2K 1
#define __USE_XOPEN2K8 1
#define __USE_LARGEFILE 1
#define __USE_MISC 1
#define __USE_ATFILE 1
#define __USE_GNU 1



#define __GLIBC_HAVE_LONG_LONG 1
#define __STDC_ISO_10646__ 201706L
#define __GLIBC_USE_DEPRECATED_GETS 0
#define __GLIBC_USE_DEPRECATED_SCANF 0
#define __GLIBC_USE_LIB_EXT2 0
#define __GLIBC_USE_ISOC11 1
#define __GLIBC_USE_ISOC99 1
#define __GLIBC_USE_ISOC23 0
#define __GLIBC_USE_ATFILE 1
#define __GLIBC_USE_POSIX_ENGINE 1
#define __GLIBC_USE_PRELINK 1
#define __GLIBC_USE_IEC_60559_BFP_EXT 0
#define __GLIBC_USE_IEC_60559_BFP_EXT_C23 0
#define __GLIBC_USE_IEC_60559_EXT 0
#define __GLIBC_USE_IEC_60559_FUNCS_EXT 0
#define __GLIBC_USE_IEC_60559_FUNCS_EXT_C23 0
#define __GLIBC_USE_IEC_60559_TYPES_EXT 0
#define __GLIBC_USE(F) __GLIBC_USE_ ## F
#endif
