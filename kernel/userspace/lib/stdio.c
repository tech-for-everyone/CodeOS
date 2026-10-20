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

/* buf_putc()/buf_padded() were the size-bounded emitters used by the old
 * vsnprintf.  They are gone because that function now uses EMIT, which
 * carries the bound itself: the measuring case (size == 0) needs the same
 * code path with an unbounded limit, which these two could not express.
 * See emit_padded() below. */

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

/* The size-bounded emitter shared by printf and vsnprintf.
 *
 * `max` carries the whole difference between the two uses: SIZE_MAX while
 * vsnprintf is MEASURING (size == 0, per C99) and `size - 1` while it is
 * formatting.  Always counting, and writing only under the bound, is what lets
 * one code path serve both -- a second copy of the formatter for the measuring
 * case would have to be kept in step with this one forever.
 *
 * That measuring case is not theoretical: the bas runtime's format! calls
 * vsnprintf(NULL, 0, fmt, ap) to size its buffer, and the old vsnprintf
 * returned 0 there, so every format! on CodeOS malloc'd one byte and printed
 * the empty string.
 */
static void emit_char(char *buf, size_t max, size_t *pos, char c) {
    if (*pos < max) buf[*pos] = c;
    (*pos)++;
}

/* Emit `len` bytes of `s` honouring zero/left padding and `width`. */
static void emit_str_padded(char *buf, size_t max, size_t *pos, const char *s,
                            int len, int zero, int left, int width) {
    int pad = (width > len) ? width - len : 0;
    int neg = (zero && !left && len > 0 && s[0] == '-');
    if (!left && !zero) { for (int i = 0; i < pad; i++) emit_char(buf, max, pos, ' '); }
    if (neg) emit_char(buf, max, pos, '-');
    if (!left && zero) { for (int i = 0; i < pad; i++) emit_char(buf, max, pos, '0'); }
    for (int i = neg ? 1 : 0; i < len; i++) emit_char(buf, max, pos, s[i]);
    if (left) { for (int i = 0; i < pad; i++) emit_char(buf, max, pos, ' '); }
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    /* size == 0 means "measure only".  The bound is then 0, NOT SIZE_MAX:
     * emit_char writes whenever pos < max, so an unbounded max would make it
     * write through the NULL buffer that the measuring caller passed.  The
     * counting is done by the unconditional (*pos)++ inside emit_char, so a
     * zero bound suppresses the write and keeps the count. */
    size_t max = (buf && size > 0) ? size - 1 : 0;

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { emit_char(buf, max, &pos, fmt[i]); continue; }
        i++;
        if (fmt[i] == '%') { emit_char(buf, max, &pos, '%'); continue; }
        int zero, left, width, llong;
        char c = parse_spec(fmt, &i, &zero, &left, &width, &llong);
        char tmp[32];
        int len;
        switch (c) {
        case 'd': case 'i': {
            int64_t v = (llong >= 2) ? va_arg(ap, long long)
                      : (llong ? va_arg(ap, long) : va_arg(ap, int));
            len = fmt_i64(tmp, v, 10, 0);
            emit_str_padded(buf, max, &pos, tmp, len, zero, left, width);
            break;
        }
        case 'u': {
            uint64_t v = (llong >= 2) ? va_arg(ap, unsigned long long)
                       : (llong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            len = fmt_u64(tmp, v, 10, 0);
            emit_str_padded(buf, max, &pos, tmp, len, zero, left, width);
            break;
        }
        case 'x': case 'X': {
            uint64_t v = (llong >= 2) ? va_arg(ap, unsigned long long)
                       : (llong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int));
            len = fmt_u64(tmp, v, 16, c == 'X');
            emit_str_padded(buf, max, &pos, tmp, len, zero, left, width);
            break;
        }
        case 'p': {
            /* The 0x prefix is part of %p's output, and it is counted like
             * any other emitted character so the measuring path stays exact.
             * Without it this printed a bare "1234" where every other libc
             * prints "0x1234" -- invisible on CodeOS, because nothing there
             * formats a pointer and compares the result to anything. */
            char hex[32];
            int hlen = fmt_u64(hex, (uint64_t)va_arg(ap, void*), 16, 0);
            emit_char(buf, max, &pos, '0');
            emit_char(buf, max, &pos, 'x');
            emit_str_padded(buf, max, &pos, hex, hlen, 1, 0, width);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            emit_str_padded(buf, max, &pos, s, slen, 0, left, width);
            break;
        }
        case 'c':
            emit_char(buf, max, &pos, (char)va_arg(ap, int));
            break;
        default:
            emit_char(buf, max, &pos, '%');
            if (c) emit_char(buf, max, &pos, c);
            break;
        }
    }
    /* The terminator only exists when there is a buffer to terminate. */
    if (buf && size > 0) buf[pos < max ? pos : max] = 0;
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
