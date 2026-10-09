#ifndef _COMPAT_STRINGS_H
#define _COMPAT_STRINGS_H 1

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif
