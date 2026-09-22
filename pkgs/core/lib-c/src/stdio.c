#include <stdint.h>
#include <stdarg.h>
#include "unistd.h"
#include "string.h"
#include "stdlib.h"

int putchar(int c) {
    char ch = (char)c;
    sys_write(&ch, 1);
    return c;
}

int puts(const char *s) {
    sys_write(s, strlen(s));
    sys_write("\n", 1);
    return 0;
}

static void print_dec(int64_t val) {
    char buf[24];
    int neg = 0;
    int pos = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { buf[pos++] = '0'; }
    while (val > 0) { buf[pos++] = '0' + (val % 10); val /= 10; }
    if (neg) buf[pos++] = '-';
    for (int i = pos - 1; i >= 0; i--) putchar(buf[i]);
}

static void print_hex(uint64_t val, int upper) {
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[20];
    int pos = 0;
    buf[pos++] = '0'; buf[pos++] = 'x';
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int d = (val >> i) & 0xf;
        if (d || started || i == 0) { started = 1; buf[pos++] = hex[d]; }
    }
    sys_write(buf, pos);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { putchar(fmt[i]); continue; }
        i++;
        int llong = 0;
        if (fmt[i] == 'l') { llong = 1; i++; }
        switch (fmt[i]) {
        case 'd': case 'i':
            print_dec(llong ? va_arg(ap, int64_t) : va_arg(ap, int));
            break;
        case 'u':
            print_dec(llong ? va_arg(ap, uint64_t) : va_arg(ap, unsigned int));
            break;
        case 'x': case 'X':
            print_hex(llong ? va_arg(ap, uint64_t) : (unsigned)va_arg(ap, int), fmt[i] == 'X');
            break;
        case 'p':
            print_hex((uint64_t)va_arg(ap, void*), 0);
            break;
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            sys_write(s, strlen(s));
            break;
        }
        case 'c':
            putchar(va_arg(ap, int));
            break;
        case '%': putchar('%'); break;
        default: putchar('%'); putchar(fmt[i]); break;
        }
    }
    va_end(ap);
    return 0;
}

int vsnprintf(char *buf, int size, const char *fmt, va_list ap) {
    if (size <= 0) return 0;
    int pos = 0;
    for (int i = 0; fmt[i] && pos < size; i++) {
        if (fmt[i] != '%') { buf[pos++] = fmt[i]; continue; }
        i++;
        switch (fmt[i]) {
        case 'd': case 'i': {
            int val = va_arg(ap, int);
            char tmp[24];
            int tpos = 0, neg = 0;
            if (val < 0) { neg = 1; val = -val; }
            if (val == 0) tmp[tpos++] = '0';
            while (val) { tmp[tpos++] = '0' + val % 10; val /= 10; }
            if (neg) tmp[tpos++] = '-';
            for (int j = tpos - 1; j >= 0 && pos < size; j--) buf[pos++] = tmp[j];
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s && pos < size) buf[pos++] = *s++;
            break;
        }
        case 'c':
            if (pos < size) buf[pos++] = va_arg(ap, int);
            break;
        default:
            if (pos < size) buf[pos++] = fmt[i];
            break;
        }
    }
    if (pos < size) {
        buf[pos] = 0;
    } else {
        buf[size - 1] = 0;
    }
    return pos;
}

int snprintf(char *buf, int size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return ret;
}
