#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memset(void *ptr, int val, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int sprintf(char *buf, const char *fmt, ...);

char *strdup(const char *s);

int strncasecmp(const char *a, const char *b, size_t n);
int strcasecmp(const char *a, const char *b);

/* Safe string functions with bounds checking */
int strncpy_safe(char *dst, const char *src, size_t dstsize);
int strncat_safe(char *dst, const char *src, size_t dstsize);

/* Bounded variants: always null-terminate, return written length */
size_t strlcpy(char *dst, const char *src, size_t dstsize);
size_t strlcat(char *dst, const char *src, size_t dstsize);

/* snprintf: write at most size-1 chars, always null-terminate, return would-be length */
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* Memory search/comparison */
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif
