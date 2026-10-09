#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void abort(void);
void exit(int status);
void _exit(int status);
int atoi(const char *nptr);
long atol(const char *nptr);
long int strtol(const char *nptr, char **endptr, int base);
unsigned long int strtoul(const char *nptr, char **endptr, int base);
int abs(int j);
long labs(long j);
char *getenv(const char *name);
int atexit(void (*func)(void));
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
int rand(void);
void srand(unsigned int seed);
int rand_r(unsigned int *seedp);
#endif
