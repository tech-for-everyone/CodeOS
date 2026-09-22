#include <stddef.h>

extern void *malloc(size_t);
extern void free(void *);

size_t strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (;; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        if (!*a) return 0;
    }
}

int strncasecmp(const char *a, const char *b, size_t n) {
    if (n == 0) return 0;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (size_t i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (!a[i]) return -1;
        if (!b[i]) return 1;
        int ca = (a[i] >= 'A' && a[i] <= 'Z') ? a[i] + 32 : a[i];
        int cb = (b[i] >= 'A' && b[i] <= 'Z') ? b[i] + 32 : b[i];
        if (ca != cb) return ca - cb;
    }
    return 0;
}

char *strcpy(char *d, const char *s) {
    if (!d || !s) return d;
    char *orig = d;
    while ((*d++ = *s++));
    return orig;
}

char *strncpy(char *d, const char *s, size_t n) {
    if (!d || !s) return d;
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strncpy_safe(char *d, const char *s, size_t n) {
    if (!d || !s || n == 0) return d;
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s) {
    if (!d || !s) return d;
    char *orig = d;
    while (*d) d++;
    while ((*d++ = *s++));
    return orig;
}

char *strchr(const char *s, int c) {
    if (!s) return 0;
    while (*s) { if (*s == c) return (char *)s; s++; }
    return 0;
}

char *strrchr(const char *s, int c) {
    if (!s) return 0;
    const char *last = 0;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return 0;
}

void *memset(void *s, int c, size_t n) {
    if (!s) return 0;
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

void *memcpy(void *d, const void *s, size_t n) {
    if (!d || !s) return d;
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    if (!d || !s) return d;
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) {
        for (size_t i = 0; i < n; i++) dd[i] = ss[i];
    } else if (dd > ss) {
        for (size_t i = n; i > 0; i--) dd[i - 1] = ss[i - 1];
    }
    return d;
}

int memcmp(const void *a, const void *b, size_t n) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    if (!s) return 0;
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    }
    return 0;
}

char *strdup(const char *s) {
    if (!s) return 0;
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (!d) return 0;
    return memcpy(d, s, len);
}