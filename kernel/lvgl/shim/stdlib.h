/* stdlib.h — freestanding shim for LVGL and third-party kernel code.
 * The cross compiler has no hosted stdlib; the kernel provides malloc/free
 * via the heap (mm.h) and mem* via string.h. */
#ifndef CODEOS_STDLIB_H
#define CODEOS_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NULL ((void *)0)

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

int atoi(const char *s);
long strtol(const char *nptr, char **endptr, int base);
void abort(void);
void exit(int status);

#ifdef __cplusplus
}
#endif

#endif
