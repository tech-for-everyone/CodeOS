#ifndef OW_URL_H
#define OW_URL_H

#include <stddef.h>

/* Small, allocation-free URL helpers for the OpenWeb frontends.
 *
 * Kept static inline in a header so the freestanding tty app and the host test
 * compile exactly the same code.  No libc calls: the userspace build has no
 * headers beyond the tiny CodeOS ones.
 */

/* Schemes the OpenWeb backend understands.  A colon after an arbitrary word
 * is NOT a scheme: "localhost:8080" and "10.0.2.2:80" are hosts with ports. */
static inline int ow_url_known_scheme(const char *url, size_t *scheme_len) {
    static const char *const schemes[] = {
        "http://", "https://", "file://", "data://", "about:", "mailto:"
    };
    size_t i;
    if (!url) return 0;
    for (i = 0; i < sizeof(schemes) / sizeof(schemes[0]); i++) {
        size_t n = 0;
        while (schemes[i][n] && url[n] && url[n] == schemes[i][n]) n++;
        if (schemes[i][n] == 0) {
            if (scheme_len) *scheme_len = n;
            return 1;
        }
    }
    return 0;
}

static inline size_t ow_url_copy(char *dst, size_t dst_size,
                                 const char *src, size_t src_len) {
    size_t n = 0;
    if (!dst || dst_size == 0 || !src) return 0;
    while (n < src_len && n + 1 < dst_size && src[n]) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
    return n;
}

static inline int ow_url_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Copy src into dst without leading/trailing ASCII whitespace. */
static inline void ow_url_trim_copy(char *dst, size_t dst_size, const char *src) {
    size_t start = 0, end = 0;
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[start] && ow_url_is_space(src[start])) start++;
    end = start;
    while (src[end]) end++;
    while (end > start && ow_url_is_space(src[end - 1])) end--;
    ow_url_copy(dst, dst_size, src + start, end - start);
}

static inline int ow_url_all_digits(const char *s) {
    size_t i = 0;
    if (!s || !s[0]) return 0;
    for (i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
    }
    return 1;
}

/* Resolve an href (from <a href=...>) against the current page URL.
 *
 * The Rust backend's ensure_scheme() only recognises absolute URLs, so a
 * relative href such as "/docs.html" reaches it mangled.  Do the RFC-style
 * join here instead; dot segments are collapsed before handing it over.
 * `out` must not alias `base` or `href`: the destination is cleared first.
 */
static inline void ow_url_resolve(const char *base, const char *href,
                                  char *out, size_t out_size) {
    char clean[512];
    char parts[512];
    size_t scheme_len = 0, base_len, host_end;
    size_t base_path_start, base_path_end, authority_end;
    size_t i, n = 0, slash;
    const char *tail;

    if (!out || out_size == 0) return;
    out[0] = 0;
    if (!href) return;

    ow_url_trim_copy(clean, sizeof(clean), href);
    if (!clean[0]) {
        if (base) ow_url_copy(out, out_size, base, 512);
        return;
    }

    /* Already absolute.  A protocol-relative URL needs the base scheme. */
    if (ow_url_known_scheme(clean, &scheme_len)) {
        ow_url_copy(out, out_size, clean, 512);
        return;
    }
    if (clean[0] == '/' && clean[1] == '/') {
        if (!base || !ow_url_known_scheme(base, &scheme_len)) {
            ow_url_copy(out, out_size, clean, 512);
            return;
        }
        /* The href already carries the "//"; keep only "scheme:". */
        size_t scheme_name_len = scheme_len;
        while (scheme_name_len > 0 && base[scheme_name_len - 1] == '/') scheme_name_len--;
        ow_url_copy(parts, sizeof(parts), base, scheme_name_len);
        n = scheme_name_len;
        ow_url_copy(parts + n, sizeof(parts) - n, clean, 512);
        ow_url_copy(out, out_size, parts, 512);
        return;
    }

    if (!base || !ow_url_known_scheme(base, &scheme_len)) {
        ow_url_copy(out, out_size, clean, 512);
        return;
    }

    base_len = 0;
    while (base[base_len] && base_len < 512) base_len++;
    host_end = scheme_len;
    while (host_end < base_len && base[host_end] != '/' &&
           base[host_end] != '?' && base[host_end] != '#') host_end++;
    base_path_start = host_end;
    base_path_end = base_path_start;
    while (base_path_end < base_len && base[base_path_end] != '?' &&
           base[base_path_end] != '#') base_path_end++;

    /* Query/fragment-only references keep the whole base path. */
    if (clean[0] == '?' || clean[0] == '#') {
        ow_url_copy(parts, sizeof(parts), base, base_path_end);
        n = base_path_end;
        ow_url_copy(parts + n, sizeof(parts) - n, clean, 512);
        ow_url_copy(out, out_size, parts, 512);
        return;
    }

    /* Scheme + authority are always kept; ".." may never eat them. */
    ow_url_copy(parts, sizeof(parts), base, base_path_start);
    n = base_path_start;
    authority_end = n;

    if (clean[0] == '/') {
        tail = clean;                       /* absolute path */
    } else {
        /* Directory of the current document, including its trailing slash. */
        slash = base_path_start;
        for (i = base_path_start; i < base_path_end; i++) {
            if (base[i] == '/') slash = i;
        }
        if (slash == base_path_start && base_path_start == base_len) {
            parts[n++] = '/';
            parts[n] = 0;
        } else {
            size_t dir_len = slash + 1 - base_path_start;
            ow_url_copy(parts + n, sizeof(parts) - n,
                        base + base_path_start, dir_len);
            n += dir_len;
        }
        tail = clean;
    }

    /* Collapse "." and ".." segments in the path portion of the reference. */
    for (i = 0; tail[i] && n + 2 < sizeof(parts); ) {
        if (tail[i] == '.' && (tail[i + 1] == '/' || tail[i + 1] == 0)) {
            i += (tail[i + 1] == '/') ? 2 : 1;
            continue;
        }
        if (tail[i] == '.' && tail[i + 1] == '.' &&
            (tail[i + 2] == '/' || tail[i + 2] == 0)) {
            while (n > authority_end && parts[n - 1] == '/') n--;
            while (n > authority_end && parts[n - 1] != '/') n--;
            /* Preserve the root slash: popping past it must not turn
             * "http://host/a/.." into "http://host". */
            if (n == authority_end) {
                parts[n++] = '/';
                parts[n] = 0;
            }
            i += (tail[i + 2] == '/') ? 3 : 2;
            continue;
        }
        parts[n++] = tail[i++];
    }
    parts[n] = 0;

    /* A bare host (no path, no query) still needs a "/" in the request. */
    if (n == authority_end) {
        parts[n++] = '/';
        parts[n] = 0;
    }

    ow_url_copy(out, out_size, parts, n);
}

#endif /* OW_URL_H */
