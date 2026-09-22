#include "kprintf.h"

static void (*output_putchar)(char) = 0;
static void (*output_clear)(void) = 0;
static void (*serial_putchar)(char) = 0;

static int capturing = 0;
static char capture_buf[4096];
static int capture_len;

void kprintf_set_output(void (*putchar_fn)(char), void (*clear_fn)(void)) {
    output_putchar = putchar_fn;
    output_clear = clear_fn;
}

void kprintf_set_serial(void (*putchar_fn)(char)) {
    serial_putchar = putchar_fn;
}

void kprintf_disable_output(void) {
    output_putchar = 0;
    output_clear = 0;
}

void kputchar(char c) {
    if (capturing) {
        if (capture_len < 4095) capture_buf[capture_len++] = c;
        capture_buf[capture_len] = 0;
        return;
    }
    if (output_putchar) output_putchar(c);
    if (serial_putchar) serial_putchar(c);
}

void kprintf_capture_begin(void) {
    capturing = 1;
    capture_len = 0;
    capture_buf[0] = 0;
}

int kprintf_capture_end(void) {
    capture_buf[capture_len] = 0;
    capturing = 0;
    return capture_len;
}

const char *kprintf_capture_get(void) {
    return capture_buf;
}

void kputs(const char *s) {
    if (!s) s = "(null)";
    while (*s) kputchar(*s++);
}

void kclear(void) {
    if (output_clear) output_clear();
}

static void print_pad(int n, char c) {
    while (n > 0) { kputchar(c); n--; }
}

static void print_dec(uint64_t val, int width, int zero_pad, int left) {
    char buf[24];
    int i = sizeof(buf) - 1;
    buf[i] = 0;
    if (val == 0) { buf[--i] = '0'; }
    else {
        while (val > 0 && i > 0) {
            buf[--i] = '0' + (val % 10);
            val /= 10;
        }
    }
    int len = sizeof(buf) - 1 - i;
    if (left) {
        while (buf[i]) kputchar(buf[i++]);
        print_pad(width - len, ' ');
    } else {
        print_pad(width - len, zero_pad ? '0' : ' ');
        while (buf[i]) kputchar(buf[i++]);
    }
}

static void print_hex(uint64_t val, int width, int left) {
    char buf[20];
    int i = sizeof(buf) - 1;
    buf[i] = 0;
    if (val == 0) { buf[--i] = '0'; }
    else {
        while (val > 0 && i > 0) {
            int d = val & 0xF;
            buf[--i] = d < 10 ? '0' + d : 'a' + d - 10;
            val >>= 4;
        }
    }
    int len = sizeof(buf) - 1 - i;
    if (left) {
        while (buf[i]) kputchar(buf[i++]);
        print_pad(width - len, ' ');
    } else {
        print_pad(width - len, '0');
        while (buf[i]) kputchar(buf[i++]);
    }
}

static void print_str(const char *str, int width, int left, int prec) {
    if (!str) str = "(null)";
    int len = 0;
    while (str[len] && (prec < 0 || len < prec)) len++;
    if (left) {
        for (int i = 0; i < len; i++) kputchar(str[i]);
        print_pad(width - len, ' ');
    } else {
        print_pad(width - len, ' ');
        for (int i = 0; i < len; i++) kputchar(str[i]);
    }
}

void kprintf(const char *fmt, ...) {
    if (!fmt || (!output_putchar && !serial_putchar)) return;
    va_list args;
    va_start(args, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { kputchar(*p); continue; }
        p++;
        if (!*p) break; /* trailing '%' — stop cleanly */
        int left = 0, zero_pad = 0, width = 0, prec = -1;
        int is_long = 0;
        for (;;) {
            if (*p == '-') { left = 1; p++; }
            else if (*p == '0') { zero_pad = 1; p++; }
            else if (*p == '*') { width = va_arg(args, int); if (width < 0) { left = 1; width = -width; } p++; }
            else if (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
            else if (*p == '.') {
                p++;
                prec = 0;
                if (*p == '*') { prec = va_arg(args, int); p++; }
                else while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); p++; }
            }
            else if (*p == 'l') { is_long++; p++; }
            else break;
        }
        if (!*p) break;
        switch (*p) {
            case 'd': {
                if (is_long >= 2) {
                    long long v = va_arg(args, long long);
                    if (v < 0) { kputchar('-'); v = -v; }
                    print_dec((uint64_t)v, width, zero_pad, left);
                } else if (is_long == 1) {
                    long v = va_arg(args, long);
                    if (v < 0) { kputchar('-'); v = -v; }
                    print_dec((uint64_t)v, width, zero_pad, left);
                } else {
                    int v = va_arg(args, int);
                    if (v < 0) { kputchar('-'); v = -v; }
                    print_dec(v, width, zero_pad, left);
                }
                break;
            }
            case 'u': {
                if (is_long >= 2)
                    print_dec(va_arg(args, unsigned long long), width, zero_pad, left);
                else if (is_long == 1)
                    print_dec(va_arg(args, unsigned long), width, zero_pad, left);
                else
                    print_dec(va_arg(args, unsigned int), width, zero_pad, left);
                break;
            }
            case 'x': {
                if (is_long >= 2)
                    print_hex(va_arg(args, unsigned long long), width, left);
                else if (is_long == 1)
                    print_hex(va_arg(args, unsigned long), width, left);
                else
                    print_hex(va_arg(args, unsigned int), width, left);
                break;
            }
            case 'p': kputs("0x"); print_hex((uint64_t)va_arg(args, void*), 16, 0); break;
            case 's': print_str(va_arg(args, const char*), width, left, prec); break;
            case 'c': kputchar((char)va_arg(args, int)); break;
            case '%': kputchar('%'); break;
            default: kputchar(*p); break;
        }
    }
    va_end(args);
}
