/* Host-side contract test for the canonical kernel adblock matcher.
 *
 * Include the real implementation so the assertions cover exact rules,
 * dot-boundary suffixes, wildcards, and enable/disable state -- not a second
 * Python model that can drift away from net.c's actual behaviour. Only the
 * kernel services ad_block.c uses (fs_read and kprintf) are stubbed out. */

extern int printf(const char *fmt, ...);

#include "../kernel/ad_block.c"

static int failures;
static int checks;

#define CHECK(expr)                                                       \
    do {                                                                  \
        checks++;                                                         \
        if (!(expr)) {                                                    \
            printf("FAIL ad_block_test:%d: %s\n", __LINE__, #expr);       \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* No /.adblock/list in the test host: built-ins only, deterministic count. */
int fs_read(const char *path, char *buf, int max) {
    (void)path;
    (void)buf;
    (void)max;
    return -1;
}

void kprintf(const char *fmt, ...) { (void)fmt; }
void kputs(const char *s) { (void)s; }
void kputchar(char c) { (void)c; }

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t i = 0;
    if (size == 0) return 0;
    while (i + 1 < size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    return i;
}

int main(void) {
    adblock_init();
    CHECK(adblock_is_enabled() == 1);
    CHECK(adblock_domain_count() == 67);

    CHECK(adblock_check_host("doubleclick.net") == 1);
    CHECK(adblock_check_host("ads.doubleclick.net") == 1);
    CHECK(adblock_check_host("DOUBLECLICK.NET.") == 1);
    CHECK(adblock_check_host("pagead2.googlesyndication.com") == 1);
    CHECK(adblock_check_host("notdoubleclick.net") == 0);
    CHECK(adblock_check_host("doubleclick.net.evil.example") == 0);
    CHECK(adblock_check_host("example.com") == 0);
    CHECK(adblock_check_host("") == 0);
    CHECK(adblock_check_host(0) == 0);

    CHECK(adblock_add_domain("*.tracker.test") == 0);
    CHECK(adblock_domain_count() == 68);
    CHECK(adblock_check_host("ads.tracker.test") == 1);
    CHECK(adblock_check_host("ads.other.test") == 0);
    CHECK(adblock_remove_domain("*.tracker.test") == 0);
    CHECK(adblock_domain_count() == 67);
    CHECK(adblock_check_host("ads.tracker.test") == 0);

    CHECK(adblock_disable() == 0);
    CHECK(adblock_is_enabled() == 0);
    CHECK(adblock_check_host("doubleclick.net") == 0);
    CHECK(adblock_enable() == 0);
    CHECK(adblock_is_enabled() == 1);
    CHECK(adblock_check_host("doubleclick.net") == 1);
    CHECK(adblock_domain_count() == 67);

    printf("ad_block checks: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
