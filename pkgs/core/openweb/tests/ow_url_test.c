/* Host-side checks for the OpenWeb URL helpers used by the tty browser.
 * Run with `make openweb-check` from the repository root. */

#include <stdio.h>
#include <string.h>
#include "../src/ow_url.h"

static int failures;
static int checks;

static void expect_str(const char *label, const char *got, const char *want) {
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %-34s got '%s', want '%s'\n", label, got, want);
    }
}

static void expect_int(const char *label, int got, int want) {
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %-34s got %d, want %d\n", label, got, want);
    }
}

struct resolve_case {
    const char *base;
    const char *href;
    const char *want;
};

int main(void) {
    char out[512];
    size_t scheme_len = 0;
    const struct resolve_case cases[] = {
        { "http://10.0.2.2:8000/docs.html", "/style.css",
          "http://10.0.2.2:8000/style.css" },
        { "http://host/dir/page.html", "next.html",
          "http://host/dir/next.html" },
        { "http://host/dir/page.html", "../other.html",
          "http://host/other.html" },
        { "http://host/dir/page.html", "./sub/../x.html",
          "http://host/dir/x.html" },
        { "http://host/dir/page.html", "?q=openweb",
          "http://host/dir/page.html?q=openweb" },
        { "http://host/dir/page.html", "#top",
          "http://host/dir/page.html#top" },
        { "http://host", "x.html", "http://host/x.html" },
        { "http://host/dir/page.html", "https://elsewhere/a",
          "https://elsewhere/a" },
        { "http://host/dir/page.html", "about:blank", "about:blank" },
        { "http://host/dir/page.html", "//cdn.example/a.png",
          "http://cdn.example/a.png" },
        { "http://host/dir/page.html", "  ../spaced.html  ",
          "http://host/spaced.html" },
        { "http://host/a/b/c.html", "../../../../x", "http://host/x" },
        { "http://host/a/b/c.html", "", "http://host/a/b/c.html" },
        { "relative/base", "next.html", "next.html" },
    };

    expect_int("scheme https", ow_url_known_scheme("https://a", &scheme_len), 1);
    expect_int("https scheme length", (int)scheme_len, 8);
    expect_int("scheme about", ow_url_known_scheme("about:blank", &scheme_len), 1);
    expect_int("about scheme length", (int)scheme_len, 6);
    expect_int("port is not a scheme", ow_url_known_scheme("localhost:8080/x", &scheme_len), 0);
    expect_int("ip port is not a scheme", ow_url_known_scheme("10.0.2.2:80/x", &scheme_len), 0);

    expect_int("digits", ow_url_all_digits("123"), 1);
    expect_int("mixed is not digits", ow_url_all_digits("12.3"), 0);
    expect_int("empty is not digits", ow_url_all_digits(""), 0);

    ow_url_trim_copy(out, sizeof(out), "  http://host/a  ");
    expect_str("trim copy", out, "http://host/a");

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ow_url_resolve(cases[i].base, cases[i].href, out, sizeof(out));
        expect_str(cases[i].href, out, cases[i].want);
    }

    printf("openweb url checks: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
