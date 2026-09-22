/* Minimal freestanding stdlib.h shim for Mbed TLS private headers
 * (alignment.h etc. include <stdlib.h> unconditionally for size_t/NULL).
 * malloc/free/calloc/realloc resolve to the CodeOS heap (kernel/mm.c). */
#ifndef _MBEDTLS_CODEOS_STDLIB_H
#define _MBEDTLS_CODEOS_STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void abort(void) __attribute__((noreturn));
#endif
