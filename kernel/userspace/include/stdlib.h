#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

int atoi(const char *s);
void *malloc(size_t size);
void free(void *p);
void exit(int status);

char *strdup(const char *s);
int snprintf(char *str, size_t size, const char *format, ...);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
char *getenv(const char *name);
extern char **environ;

#endif
