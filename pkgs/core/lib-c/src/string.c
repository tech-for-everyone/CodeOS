int strlen(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char *strncpy_safe(char *d, const char *s, int n) {
    if (n <= 0) return d;
    int i;
    for (i = 0; i < n - 1 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    return d;
}

char *strcpy(char *d, const char *s) {
    return strncpy_safe(d, s, 4096);
}

char *strncpy(char *d, const char *s, int n) {
    int i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s) {
    char *ret = d;
    int remaining = 4096;
    while (*d && remaining > 0) { d++; remaining--; }
    while (*s && remaining > 1) { *d++ = *s++; remaining--; }
    if (remaining >= 0) *d = 0;
    return ret;
}

char *strchr(const char *s, int c) {
    while (*s) { if (*s == c) return (char*)s; s++; }
    return 0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return 0;
}

void *memset(void *s, int c, int n) {
    unsigned char *p = s;
    for (int i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

void *memcpy(void *d, const void *s, int n) {
    unsigned char *dd = d;
    const unsigned char *ss = s;
    for (int i = 0; i < n; i++) dd[i] = ss[i];
    return d;
}

void *memmove(void *d, const void *s, int n) {
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) {
        for (int i = 0; i < n; i++) dd[i] = ss[i];
    } else if (dd > ss) {
        for (int i = n - 1; i >= 0; i--) dd[i] = ss[i];
    }
    return d;
}
