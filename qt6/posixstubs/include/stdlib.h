/* CodeOS C++ Standard Library Stubs for Qt6 */

#ifndef _CODEOS_STDLIB_STUBS_H
#define _CODEOS_STDLIB_STUBS_H

#include <stdint.h>

/* Basic C++ operators - declarations only.  Placement new/delete are NOT
 * defined here: they are provided inline by the host libstdc++ <new>, and
 * redefining them here conflicts with it.  Real definitions live in
 * kernel/kernel/cxx_support.cpp / qt6/posixstubs/src/cppstubs.cpp. */
void *operator new(unsigned long size);
void *operator new[](unsigned long size);
void operator delete(void *ptr);
void operator delete[](void *ptr);
void operator delete(void *ptr, unsigned long size);
void operator delete[](void *ptr, unsigned long size);

#endif /* _CODEOS_STDLIB_STUBS_H */
