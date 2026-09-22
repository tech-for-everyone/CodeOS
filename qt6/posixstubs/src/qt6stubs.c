/* CodeOS Additional POSIX Stubs for Qt6
 * Functions Qt6 needs that aren't in the main posixstubs.c */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════════════════
   ctype.h functions
   ═══════════════════════════════════════════════════════════════════ */

static const unsigned char ctype_table[256] = {
    0,0,0,0,0,0,0,0,0,0x08,0x08,0x08,0x08,0x08,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x20,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x41,0x41,0x41,0x41,0x41,0x41,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x10,0x10,0x10,0x10,0x10,
    0x10,0x42,0x42,0x42,0x42,0x42,0x42,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
    0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x10,0x10,0x10,0x10,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define _ISupper  0x01
#define _ISlower  0x02
#define _ISdigit  0x04
#define _ISspace  0x08
#define _ISprint  0x10
#define _ISalpha  0x20
#define _ISalnum  0x40

int isalnum(int c) { return ctype_table[(unsigned char)c] & _ISalnum; }
int isalpha(int c) { return ctype_table[(unsigned char)c] & _ISalpha; }
int isdigit(int c) { return ctype_table[(unsigned char)c] & _ISdigit; }
int isxdigit(int c) { return isdigit(c) || ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')); }
int isspace(int c) { return ctype_table[(unsigned char)c] & _ISspace; }
int isupper(int c) { return ctype_table[(unsigned char)c] & _ISupper; }
int islower(int c) { return ctype_table[(unsigned char)c] & _ISlower; }
int isprint(int c) { return ctype_table[(unsigned char)c] & _ISprint; }
int ispunct(int c) { unsigned char u = c; return isprint(c) && !isalnum(c) && !isspace(c); }
int iscntrl(int c) { return (unsigned char)c < 0x20 || c == 0x7F; }
int isgraph(int c) { return isprint(c) && !isspace(c); }

int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

/* ═══════════════════════════════════════════════════════════════════
   wchar.h / string wide functions
   ═══════════════════════════════════════════════════════════════════ */

#ifndef WCHAR_MAX
#define WCHAR_MAX 0x7FFFFFFF
#endif
#define WEOF ((wint_t)-1)

/* Type definitions for freestanding environment */
typedef uint32_t wint_t;
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

size_t wcslen(const wchar_t *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

int wcscmp(const wchar_t *a, const wchar_t *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? *a - *b : 0;
}

wchar_t *wcscpy(wchar_t *dst, const wchar_t *src) {
    wchar_t *d = dst;
    while ((*d++ = *src++));
    return dst;
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n) {
    wchar_t *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = 0;
    return dst;
}

wchar_t *wcscat(wchar_t *dst, const wchar_t *src) {
    wchar_t *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

int wctob(wint_t c) { return (c < 128) ? (int)c : -1; }
wint_t btowc(int c) { return (c < 0 || c > 255) ? WEOF : (wint_t)c; }

size_t wcstombs(char *dst, const wchar_t *src, size_t maxlen) {
    size_t count = 0;
    while (*src && count < maxlen) {
        wchar_t wc = *src++;
        if (wc < 0x80) {
            if (dst) dst[count] = (char)wc;
            count++;
        } else if (wc < 0x800) {
            if (dst && count + 1 < maxlen) {
                dst[count] = 0xC0 | (wc >> 6);
                dst[count+1] = 0x80 | (wc & 0x3F);
            }
            count += 2;
        } else {
            if (dst && count + 2 < maxlen) {
                dst[count] = 0xE0 | (wc >> 12);
                dst[count+1] = 0x80 | ((wc >> 6) & 0x3F);
                dst[count+2] = 0x80 | (wc & 0x3F);
            }
            count += 3;
        }
    }
    if (dst && count < maxlen) dst[count] = 0;
    return count;
}

size_t mbstowcs(wchar_t *dst, const char *src, size_t maxlen) {
    size_t count = 0;
    const unsigned char *s = (const unsigned char *)src;
    while (*s && count < maxlen) {
        wchar_t wc;
        if (*s < 0x80) {
            wc = *s++;
        } else if ((*s & 0xE0) == 0xC0) {
            wc = (*s++ & 0x1F) << 6;
            wc |= (*s++ & 0x3F);
        } else if ((*s & 0xF0) == 0xE0) {
            wc = (*s++ & 0x0F) << 12;
            wc |= (*s++ & 0x3F) << 6;
            wc |= (*s++ & 0x3F);
        } else {
            s++;
            wc = 0xFFFD;
        }
        if (dst) dst[count] = wc;
        count++;
    }
    if (dst && count < maxlen) dst[count] = 0;
    return count;
}

int mbtowc(wchar_t *dst, const char *src, size_t maxlen) {
    if (!src) return 0;
    if (!*src) return 0;
    if (maxlen == 0) return -1;
    const unsigned char *s = (const unsigned char *)src;
    wchar_t wc;
    int len;
    if (*s < 0x80) { wc = *s; len = 1; }
    else if ((*s & 0xE0) == 0xC0) { wc = (*s & 0x1F) << 6; wc |= (s[1] & 0x3F); len = 2; }
    else if ((*s & 0xF0) == 0xE0) { wc = (*s & 0x0F) << 12; wc |= (s[1] & 0x3F) << 6; wc |= (s[2] & 0x3F); len = 3; }
    else { wc = 0xFFFD; len = 1; }
    if (dst) *dst = wc;
    return len;
}

int wctomb(char *dst, wchar_t wc) {
    if (!dst) return 0;
    if (wc < 0x80) { dst[0] = (char)wc; return 1; }
    if (wc < 0x800) { dst[0] = 0xC0|(wc>>6); dst[1] = 0x80|(wc&0x3F); return 2; }
    dst[0] = 0xE0|(wc>>12); dst[1] = 0x80|((wc>>6)&0x3F); dst[2] = 0x80|(wc&0x3F);
    return 3;
}

/* ═══════════════════════════════════════════════════════════════════
   stdlib.h functions Qt6 needs
   ═══════════════════════════════════════════════════════════════════ */

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int num, int den) { return (div_t){ num/den, num%den }; }
ldiv_t ldiv(long num, long den) { return (ldiv_t){ num/den, num%den }; }
lldiv_t lldiv(long long num, long long den) { return (lldiv_t){ num/den, num%den }; }

long strtol(const char *nptr, char **endptr, int base) {
    long result = 0;
    int negative = 0;
    const char *p = nptr;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '-') { negative = 1; p++; }
    else if (*p == '+') p++;

    if (base == 0) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (*p == '0') { base = 8; p++; }
        else base = 10;
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        p++;
    }

    if (endptr) *endptr = (char *)p;
    return negative ? -result : result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoll(nptr, endptr, base);
}

double strtod(const char *nptr, char **endptr) {
    /* Minimal implementation */
    double result = 0;
    int negative = 0;
    const char *p = nptr;

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-') { negative = 1; p++; }
    else if (*p == '+') p++;

    while (*p >= '0' && *p <= '9') {
        result = result * 10.0 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        double frac = 0.1;
        while (*p >= '0' && *p <= '9') {
            result += (*p - '0') * frac;
            frac *= 0.1;
            p++;
        }
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        int exp_neg = 0;
        if (*p == '-') { exp_neg = 1; p++; }
        else if (*p == '+') p++;
        int exp = 0;
        while (*p >= '0' && *p <= '9') { exp = exp * 10 + (*p - '0'); p++; }
        while (exp-- > 0) { if (exp_neg) result /= 10.0; else result *= 10.0; }
    }

    if (endptr) *endptr = (char *)p;
    return negative ? -result : result;
}

float strtof(const char *nptr, char **endptr) {
    return (float)strtod(nptr, endptr);
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }
long long atoll(const char *s) { return strtoll(s, 0, 10); }

void exit(int status) {
    extern void _exit(int status);
    _exit(status);
    while(1);
}

void abort(void) {
    extern void _exit(int status);
    _exit(1);
    while(1);
}

int atexit(void (*func)(void)) { (void)func; return 0; }

void *calloc(size_t nmemb, size_t size) {
    extern void *malloc(size_t size);
    extern void *memset(void *s, int c, size_t n);
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    extern void *malloc(size_t size);
    extern void free(void *p);
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    void *newp = malloc(size);
    if (newp) {
        extern void *memcpy(void *dst, const void *src, size_t n);
        /* Copy up to 'size' bytes - we don't know original size, assume size */
        memcpy(newp, ptr, size);
        free(ptr);
    }
    return newp;
}

/* ═══════════════════════════════════════════════════════════════════
   errno.h stub
   ═══════════════════════════════════════════════════════════════════ */

static int _errno_val = 0;
int *__errno_location(void) { return &_errno_val; }

#define EDOM    33
#define ERANGE  34
#define EILSEQ  84
#define EINVAL  22
#define ENOENT   2
#define ENOMEM  12
#define ENOSYS  38
#define EAGAIN  11
#define EIO      5
#define ENOEXEC  8
#define EBADF    9
#define ECHILD  10
#define EACCES  13
#define ENOTDIR 20
#define EISDIR  21
#define EFBIG   27
#define ENOSPC  28
#define ESPIPE  29
#define EROFS   30
#define ENAMETOOLONG  36
#define ENOTEMPTY 39

/* ═══════════════════════════════════════════════════════════════════
   fcntl.h stubs
   ═══════════════════════════════════════════════════════════════════ */

#define O_RDONLY   0
#define O_WRONLY   1
#define O_RDWR     2
#define O_CREAT    0100
#define O_TRUNC    01000
#define O_APPEND   02000
#define O_NONBLOCK 04000
#define O_CLOEXEC  02000000

int fcntl(int fd, int cmd, ...) {
    (void)fd; (void)cmd;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
   termios stubs
   ═══════════════════════════════════════════════════════════════════ */

struct termios {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_cc[32];
    uint32_t c_ispeed, c_ospeed;
};

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCIFLUSH 0
#define TCIOFLUSH 1
#define TCOFLUSH 2
#define B38400 017

int tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (t) { extern void *memset(void *s, int c, size_t n); memset(t, 0, sizeof(*t)); }
    return 0;
}

int tcsetattr(int fd, int action, const struct termios *t) {
    (void)fd; (void)action; (void)t;
    return 0;
}

int tcflush(int fd, int action) {
    (void)fd; (void)action;
    return 0;
}

int cfgetospeed(const struct termios *t) { (void)t; return B38400; }
int cfgetispeed(const struct termios *t) { (void)t; return B38400; }
int cfsetospeed(struct termios *t, int speed) { (void)t; (void)speed; return 0; }
int cfsetispeed(struct termios *t, int speed) { (void)t; (void)speed; return 0; }

/* ═══════════════════════════════════════════════════════════════════
   Additional string functions Qt6 uses
   ═══════════════════════════════════════════════════════════════════ */

long double strtold(const char *nptr, char **endptr) {
    return (long double)strtod(nptr, endptr);
}

/* ═══════════════════════════════════════════════════════════════════
   Additional math stubs (minimal)
   ═══════════════════════════════════════════════════════════════════ */

double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }
long double fabsl(long double x) { return x < 0 ? -x : x; }

double sqrt(double x) {
    if (x < 0) return 0;
    double guess = x / 2.0;
    for (int i = 0; i < 50; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

float sqrtf(float x) { return (float)sqrt((double)x); }

double pow(double base, double exp) {
    if (exp == 0) return 1.0;
    if (exp == 1) return base;
    double result = base;
    for (int i = 1; i < (int)exp; i++) result *= base;
    return result;
}

float powf(float base, float exp) { return (float)pow((double)base, (double)exp); }

double log(double x) {
    /* Newton's method approximation */
    if (x <= 0) return -1e308;
    double result = 0;
    while (x > 2.0) { x /= 2.718281828; result += 1.0; }
    while (x < 0.5) { x *= 2.718281828; result -= 1.0; }
    x -= 1.0;
    double term = x, sum = x;
    for (int i = 2; i < 20; i++) {
        term *= -x * (i-1) / i;
        sum += term / i;
    }
    return result + sum;
}

double log2(double x) { return log(x) / log(2.0); }
double log10(double x) { return log(x) / log(10.0); }

double ceil(double x) {
    int i = (int)x;
    return (x > 0 && x != i) ? i + 1.0 : i;
}

double floor(double x) {
    int i = (int)x;
    return (x < 0 && x != i) ? i - 1.0 : i;
}

double round(double x) {
    if (x < 0) return -floor(-x + 0.5);
    return floor(x + 0.5);
}

float ceilf(float x) { return (float)ceil((double)x); }
float floorf(float x) { return (float)floor((double)x); }
float roundf(float x) { return (float)round((double)x); }

double trunc(double x) { return (double)(long long)x; }
float truncf(float x) { return (float)(long long)x; }

double fmod(double x, double y) {
    if (y == 0) return 0;
    return x - (long long)(x / y) * y;
}

float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }

double sin(double x) {
    /* Taylor series */
    double sum = 0, term = x;
    for (int i = 0; i < 15; i++) {
        sum += term;
        term *= -x * x / ((2*i+2)*(2*i+3));
    }
    return sum;
}

double cos(double x) { return sin(x + 1.570796327); }
float sinf(float x) { return (float)sin((double)x); }
float cosf(float x) { return (float)cos((double)x); }
double atan(double x) { return x; /* stub */ }
double atan2(double y, double x) { (void)y; (void)x; return 0; /* stub */ }
double tan(double x) { (void)x; return 0; /* stub */ }
