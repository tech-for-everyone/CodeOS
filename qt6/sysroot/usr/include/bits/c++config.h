// Wrapper for libstdc++ config that overrides hosted assumptions
#ifndef _BITS_CXXCONFIG_WRAPPER_H
#define _BITS_CXXCONFIG_WRAPPER_H 1

// Directly include the system one to get its defines
#include "/usr/include/c++/16/x86_64-pc-linux-gnu/bits/c++config.h"

// Override problematic settings for freestanding compilation
// Undefine wchar support entirely (#ifdef would pass with 0, #undef makes it fail)
#undef _GLIBCXX_USE_WCHAR_T
#undef _GLIBCXX_HAVE_WCHAR_H
#undef _GLIBCXX_HAVE_WCSTOF
#undef _GLIBCXX_HAVE_VFWSCANF
#undef _GLIBCXX_HAVE_VSWSCANF
#undef _GLIBCXX_HAVE_VWSCANF
#undef _GLIBCXX_USE_C99_WCHAR
#undef _GLIBCXX_USE_C99_STDINT

// These are used with #if (not #ifdef), so 0 is fine
#define _GLIBCXX_USE_C99_WCHAR 0

#endif // _BITS_CXXCONFIG_WRAPPER_H
