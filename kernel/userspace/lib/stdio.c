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
    if (!s) return sys_write("(null)\n", 7);
    sys_write(s, strlen(s));
    sys_write("\n", 1);
    return 0;
}

/* ── number formatting into a small buffer ──
 * The plain `%x`/`%X`/`%p` conversions keep their historical "0x" prefix so
 * existing callers are unaffected; widths zero/space pad to the field. */

static int fmt_u64(char *out, uint64_t v, int base, int upper) {
    char tmp[24];
    int t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v) {
        int d = (int)(v % (uint64_t)base);
        tmp[t++] = (d < 10) ? (char)('0' + d)
                            : (char)((upper ? 'A' : 'a') + (d - 10));
        v /= (uint64_t)base;
    }
    int n = 0;
    while (t > 0) out[n++] = tmp[--t];
    return n;
}

static int fmt_i64(char *out, int64_t v, int base, int upper) {
    if (base == 10 && v < 0) {
        out[0] = '-';
        return 1 + fmt_u64(out + 1, (uint64_t)(-v), base, upper);
    }
    return fmt_u64(out, (uint64_t)v, base, upper);
}

static void emit_padded(const char *s, int len, int zero, int left, int width) {
    int pad = (width > len) ? width - len : 0;
    int neg = (zero && !left && len > 0 && s[0] == '-');
    if (!left && !zero) { for (int i = 0; i < pad; i++) putchar(' '); }
    if (neg) putchar('-');
    if (!left && zero) { for (int i = 0; i < pad; i++) putchar('0'); }
    for (int i = neg ? 1 : 0; i < len; i++) putchar(s[i]);
    if (left) { for (int i = 0; i < pad; i++) putchar(' '); }
}

static void buf_putc(char *buf, size_t size, size_t *pos, char c) {
    if (*pos < size - 1) buf[(*pos)++] = c;
}

static void buf_padded(char *buf, size_t size, size_t *pos, const char *s,
                       int len, int zero, int left, int width) {
    int pad = (width > len) ? width - len : 0;
    int neg = (zero && !left && len > 0 && s[0] == '-');
    if (!left && !zero) { for (int i = 0; i < pad; i++) buf_putc(buf, size, pos, ' '); }
    if (neg) buf_putc(buf, size, pos, '-');
    if (!left && zero) { for (int i = 0; i < pad; i++) buf_putc(buf, size, pos, '0'); }
    for (int i = neg ? 1 : 0; i < len; i++) buf_putc(buf, size, pos, s[i]);
    if (left) { for (int i = 0; i < pad; i++) buf_putc(buf, size, pos, ' '); }
}

/* Parse the flags/width/precision/length-modifier of one conversion.
 * Returns the conversion char and stores the parsed field settings. */
static char parse_spec(const char *fmt, int *pi, int *zero, int *left, int *width, int *llong) {
    int i = *pi;
    *zero = 0; *left = 0; *width = 0; *llong = 0;
    for (;;) {
        if (fmt[i] == '0') { *zero = 1; i++; }
        else if (fmt[i] == '-') { *left = 1; i++; }
        else if (fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#') { i++; }
        else break;
    }
    while (fmt[i] >= '0' && fmt[i] <= '9') { *width = *width * 10 + (fmt[i] - '0'); i++; }
    if (fmt[i] == '.') { i++; while (fmt[i] >= '0' && fmt[i] <= '9') i++; }
    while (fmt[i] == 'l') { (*llong)++; i++; }
    while (fmt[i] == 'z' || fmt[i] == 'h' || fmt[i] == 'j' || fmt[i] == 't') { i++; }
    *pi = i;
    return fmt[i];
}

int printf(const char *fmt, ...) {
    if (!fmt) return 0;
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { putchar(fmt[i]); continue; }
        i++;
        if (fmt[i] == '%') { putchar('%'); continue; }
        int zero, left, width, llong;
        char c = parse_spec(fmt, &i, &zero, &left, &width, &llong);
        char tmp[32];
        int len;
        switch (c) {
        case 'd': case 'i': {
            int64_t v = (llong >= 2) ? va_arg(ap, long long)
                      : (llong ? va_arg(ap, long) : va_arg(ap, int));
            len = fmt_i64(tmp, v, 10, 0);
            emit_padded(tmp, len, zero, left, width);
            break;
        }
        case 'u': {
            uint64_t v = (llong >= 2) ? va_arg(ap, unsigned long long)
                       : (llong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            len = fmt_u64(tmp, v, 10, 0);
            emit_padded(tmp, len, zero, left, width);
            break;
        }
        case 'x': case 'X': {
            uint64_t v = (llong >= 2) ? va_arg(ap, unsigned long long)
                       : (llong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            tmp[0] = '0'; tmp[1] = 'x';
            len = 2 + fmt_u64(tmp + 2, v, 16, c == 'X');
            emit_padded(tmp, len, zero, left, width);
            break;
        }
        case 'p': {
            tmp[0] = '0'; tmp[1] = 'x';
            len = 2 + fmt_u64(tmp + 2, (uint64_t)va_arg(ap, void*), 16, 0);
            emit_padded(tmp, len, zero, left, width);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            int pad = (width > slen) ? width - slen : 0;
            if (!left) for (int k = 0; k < pad; k++) putchar(' ');
            sys_write(s, slen);
            if (left) for (int k = 0; k < pad; k++) putchar(' ');
            break;
        }
        case 'c': {
            int ch = va_arg(ap, int);
            int pad = (width > 1) ? width - 1 : 0;
            if (!left) for (int k = 0; k < pad; k++) putchar(' ');
            putchar(ch);
            if (left) for (int k = 0; k < pad; k++) putchar(' ');
            break;
        }
        default:
            putchar('%'); if (c) putchar(c);
            break;
        }
    }
    va_end(ap);
    return 0;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    if (!buf || size == 0) return 0;
    size_t pos = 0;
    for (int i = 0; fmt[i] && pos < size - 1; i++) {
        if (fmt[i] != '%') { buf[pos++] = fmt[i]; continue; }
        i++;
        if (fmt[i] == '%') { buf[pos++] = '%'; continue; }
        int zero, left, width, llong;
        char c = parse_spec(fmt, &i, &zero, &left, &width, &llong);
        char tmp[32];
        int len;
        switch (c) {
        case 'd': case 'i': {
            int64_t v = (llong >= 2) ? va_arg(ap, long long)
                      : (llong ? va_arg(ap, long) : va_arg(ap, int));
            len = fmt_i64(tmp, v, 10, 0);
            buf_padded(buf, size, &pos, tmp, len, zero, left, width);
            break;
        }
        case 'u': {
            uint64_t v = (llong >= 2) ? va_arg(ap, unsigned long long)
                       : (llong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            len = fmt_u64(tmp, v, 10, 0);
            buf_padded(buf, size, &pos, tmp, len, zero, left, width);
            break;
        }
        case 'x': case 'X': {
            uint64_t v = (llong >= 2) ? va_arg(ap, unsigned long long)
                       : (llong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            len = fmt_u64(tmp, v, 16, c == 'X');
            buf_padded(buf, size, &pos, tmp, len, zero, left, width);
            break;
        }
        case 'p': {
            len = fmt_u64(tmp, (uint64_t)va_arg(ap, void*), 16, 0);
            buf_padded(buf, size, &pos, tmp, len, zero, left, width);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            int pad = (width > slen) ? width - slen : 0;
            if (!left) for (int k = 0; k < pad; k++) buf_putc(buf, size, &pos, ' ');
            for (int k = 0; k < slen; k++) buf_putc(buf, size, &pos, s[k]);
            if (left) for (int k = 0; k < pad; k++) buf_putc(buf, size, &pos, ' ');
            break;
        }
        case 'c':
            buf_putc(buf, size, &pos, (char)va_arg(ap, int));
            break;
        default:
            buf_putc(buf, size, &pos, '%');
            if (c) buf_putc(buf, size, &pos, c);
            break;
        }
    }
    buf[pos] = 0;
    return (int)pos;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}
