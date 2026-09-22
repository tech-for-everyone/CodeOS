#ifndef _STRING_H
#define _STRING_H

int strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, int n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, int n);
char *strncpy_safe(char *d, const char *s, int n);
char *strcat(char *d, const char *s);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memset(void *s, int c, int n);
void *memcpy(void *d, const void *s, int n);
void *memmove(void *d, const void *s, int n);

#endif
