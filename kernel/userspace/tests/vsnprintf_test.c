/* Differential test for the rewritten vsnprintf.
 *
 * The freestanding implementation is compiled here against a real libc, and
 * every case is compared with what glibc's snprintf produces for the SAME
 * format.  That catches the errors a CodeOS boot log cannot: a wrong measured
 * length, a dropped padding character, or a %s that measures short would all
 * still "work" on CodeOS, because nothing there ever compares them to anything.
 *
 * Run: cc -std=c11 -Wall -Wextra -o /tmp/vsnprintf_test \
 *         kernel/userspace/tests/vsnprintf_test.c && /tmp/vsnprintf_test
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* sys_write is stubbed because lib/stdio.c's putchar/puts call it.  Only
 * vsnprintf is under test. */
static int sys_write(const void *buf, int count) {
    (void)buf;
    return count;
}

/* The implementation under test, renamed so both can live in one binary.
 * printf is renamed as well: lib/stdio.c defines its own, which writes through
 * the stubbed sys_write and would produce no output at all, leaving the test
 * silent and looking like a pass with zero checks. */
#define vsnprintf freestanding_vsnprintf
#define printf    freestanding_printf
#define sprintf   freestanding_sprintf
#define snprintf  freestanding_snprintf
#include "../lib/stdio.c"
#undef snprintf
#undef sprintf
#undef printf
#undef vsnprintf

static int failures;
static int checks;

/* Forward to the implementation under test.  These are real variadic functions
 * rather than va_start inside a macro: a macro-expanded format string is not a
 * parameter name, so `va_start(ap, fmtstr)` in a macro is a constraint
 * violation ("va_start used in function with fixed arguments"). */
static int impl(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = freestanding_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* The C99 measuring case: size == 0 returns the length the result needs. */
static int measure(const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = freestanding_vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    return n;
}

static int failures;
static int checks;

/* One case: measure, format, and compare both against the host libc.
 * `,##__VA_ARGS__` is the GNU comma-swallow, so the no-argument cases work
 * without a separate macro. */
#define CMP(fmtstr, ...)                                                \
    do {                                                                \
        char m[256], r[256];                                            \
        int meas, writ, refw;                                           \
        meas = measure(fmtstr, ##__VA_ARGS__);                          \
        writ = impl(m, sizeof(m), fmtstr, ##__VA_ARGS__);               \
        refw = snprintf(r, sizeof(r), fmtstr, ##__VA_ARGS__);           \
        checks++;                                                       \
        if (meas != refw || writ != refw || strcmp(m, r) != 0) {        \
            failures++;                                                 \
            printf("FAIL %-24s measured=%d written=%d ref=%d  "         \
                   "mine=[%s] ref=[%s]\n",                              \
                   #fmtstr, meas, writ, refw, m, r);                    \
        }                                                               \
    } while (0)

int main(void) {
    /* The measuring case (size == 0) is what bas's format! calls to size its
     * buffer, and it is what the old implementation got wrong. */
    CMP("");
    CMP("hello");
    CMP("%d", 42);
    CMP("%s", "abcdef");
    CMP("{}+{}={}", 2, 3, 5);

    /* Rendering. */
    CMP("no conversions here");
    CMP("%d", 12345);
    CMP("%d", -99);
    CMP("%d", 0);
    CMP("%u", 4000000000u);
    CMP("%x", 0xdeadbeefu);
    CMP("%X", 0xdeadbeefu);
    CMP("%ld", 123456789L);
    CMP("%s", "the quick brown fox");
    CMP("%s", "");
    CMP("%c", 'q');
    CMP("100%%");
    CMP("%d/%d/%d", 1, -2, 3);
    CMP("{}", 7);
    CMP("%p", (void *)0x1234);

    /* Padding: the measuring path has to count it exactly as the formatting
     * path writes it, which is exactly where a shared bound can drift. */
    CMP("%5d", 42);
    CMP("%05d", 42);
    CMP("%5d", -42);
    CMP("%05d", -42);
    CMP("%-5d|", 42);
    CMP("%8s|", "ab");

    /* Truncation must not overflow and must not report a short length. */
    checks++;
    {
        char small[4];
        int n = impl(small, sizeof(small), "abcdefgh");
        if (n != 8 || strcmp(small, "abc") != 0) {
            failures++;
            printf("FAIL truncate             wrote=%d [%s]\n", n, small);
        }
    }

    /* A NULL buffer with a non-zero size must still measure, not fault. */
    checks++;
    {
        int n = impl(NULL, 16, "%d", 7);
        if (n != 1) {
            failures++;
            printf("FAIL null buf nonzero size got=%d\n", n);
        }
    }

    if (failures) {
        printf("FAIL: %d of %d checks\n", failures, checks);
        return 1;
    }
    printf("PASS: %d vsnprintf checks (measuring + formatting)\n", checks);
    return 0;
}