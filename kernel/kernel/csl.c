#include "csl.h"
#include "script.h"
#include "shell.h"
#include "string.h"
#include "kprintf.h"
#include "fs.h"
#include "mm.h"
#include "sched.h"
#include "x11_server.h"
#include "penrose_bridge.h"
#include "../drivers/timer.h"

/* ---- value helpers ---- */

static script_val_t r_num(int64_t n) {
    script_val_t v; v.type = 0; v.num = n; v.str = 0; return v;
}
static script_val_t r_owned(char *s) {
    script_val_t v; v.type = 1; v.num = 0; v.str = s; return v;
}
static script_val_t r_str(const char *s) {
    script_val_t v; v.type = 1; v.num = 0; v.str = s ? strdup(s) : 0; return v;
}

/* int64 -> string inside buf (>=24 bytes), returns a pointer into buf */
static char *i2s(int64_t n, char *buf, int bufsz) {
    int i = bufsz - 1;
    buf[i] = 0;
    uint64_t u = (n < 0) ? (uint64_t)(-(n + 1)) + 1 : (uint64_t)n;
    do { buf[--i] = (char)('0' + (u % 10)); u /= 10; } while (u && i > 0);
    if (n < 0 && i > 0) buf[--i] = '-';
    return buf + i;
}

static int64_t csl_atoll(const char *s) {
    int64_t r = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') r = r * 10 + (*s++ - '0');
    return r * sign;
}

/* Copy arg idx into out (string args copied, number args formatted). */
static int arg_str(script_val_t *a, int n, int idx, char *out, int outsz) {
    if (!out || outsz <= 0) return 0;
    if (idx < 0 || idx >= n) { out[0] = 0; return 0; }
    if (a[idx].type == 1) {
        strlcpy(out, a[idx].str ? a[idx].str : "", outsz);
        return 1;
    }
    char tmp[24];
    strlcpy(out, i2s(a[idx].num, tmp, sizeof(tmp)), outsz);
    return 1;
}

static int64_t arg_num(script_val_t *a, int n, int idx, int64_t dflt) {
    if (idx < 0 || idx >= n) return dflt;
    if (a[idx].type == 0) return a[idx].num;
    if (a[idx].type == 1 && a[idx].str) return csl_atoll(a[idx].str);
    return dflt;
}

/* ---- string natives ---- */

static script_val_t n_str(script_val_t *a, int n) {
    if (n < 1) return r_str("");
    if (a[0].type == 1) return r_str(a[0].str ? a[0].str : "");
    char b[24];
    return r_str(i2s(a[0].num, b, sizeof(b)));
}

static script_val_t n_num(script_val_t *a, int n) {
    return r_num(arg_num(a, n, 0, 0));
}

static script_val_t n_len(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    return r_num(script_repr_count(buf));
}

static int c_up(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int c_lo(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static script_val_t n_upper(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    for (char *p = buf; *p; p++) *p = (char)c_up(*p);
    return r_str(buf);
}

static script_val_t n_lower(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    for (char *p = buf; *p; p++) *p = (char)c_lo(*p);
    return r_str(buf);
}

static script_val_t n_trim(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    char *s = buf, *e = buf + strlen(buf);
    while (*s == ' ' || *s == '\t') s++;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
    *e = 0;
    return r_str(s);
}

static script_val_t n_reverse(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    int len = (int)strlen(buf);
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char t = buf[i]; buf[i] = buf[j]; buf[j] = t;
    }
    return r_str(buf);
}

static script_val_t n_substr(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    int64_t start = arg_num(a, n, 1, 0);
    int64_t count = arg_num(a, n, 2, -1);
    int64_t len = (int64_t)strlen(buf);
    if (start < 0) start += len;
    if (start < 0) start = 0;
    if (start > len) return r_str("");
    int64_t end = (count < 0) ? len : start + count;
    if (end > len) end = len;
    int64_t outlen = end - start;
    char *out = malloc((size_t)outlen + 1);
    if (!out) return r_str("");
    for (int64_t i = 0; i < outlen; i++) out[i] = buf[start + i];
    out[outlen] = 0;
    return r_owned(out);
}

static script_val_t n_instr(script_val_t *a, int n) {
    char h[1024], nd[256];
    arg_str(a, n, 0, h, sizeof(h));
    arg_str(a, n, 1, nd, sizeof(nd));
    if (!nd[0]) return r_num(0);
    char *p = strstr(h, nd);
    return r_num(p ? (int64_t)(p - h) : -1);
}

static script_val_t n_replace(script_val_t *a, int n) {
    char s[1024], fr[256], to[256];
    arg_str(a, n, 0, s, sizeof(s));
    arg_str(a, n, 1, fr, sizeof(fr));
    arg_str(a, n, 2, to, sizeof(to));
    if (!fr[0]) return r_str(s);
    char *hit = strstr(s, fr);
    if (!hit) return r_str(s);
    char out[1400];
    int p = 0;
    for (char *q = s; q < hit && p < 1390;) out[p++] = *q++;
    for (char *q = to; *q && p < 1390;) out[p++] = *q++;
    hit += strlen(fr);
    for (; *hit && p < 1390;) out[p++] = *hit++;
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_contains(script_val_t *a, int n) {
    char h[1024], nd[256];
    arg_str(a, n, 0, h, sizeof(h));
    arg_str(a, n, 1, nd, sizeof(nd));
    return r_num(strstr(h, nd) ? 1 : 0);
}

static script_val_t n_startswith(script_val_t *a, int n) {
    char s[1024], pre[256];
    arg_str(a, n, 0, s, sizeof(s));
    arg_str(a, n, 1, pre, sizeof(pre));
    return r_num(strncmp(s, pre, strlen(pre)) == 0);
}

static script_val_t n_endswith(script_val_t *a, int n) {
    char s[1024], suf[256];
    arg_str(a, n, 0, s, sizeof(s));
    arg_str(a, n, 1, suf, sizeof(suf));
    int sl = (int)strlen(s), tl = (int)strlen(suf);
    if (tl > sl || !tl) return r_num(tl == 0);
    return r_num(strncmp(s + sl - tl, suf, tl) == 0);
}

static script_val_t n_chrat(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    int64_t i = arg_num(a, n, 1, 0);
    int64_t len = (int64_t)strlen(buf);
    if (i < 0) i += len;
    if (i < 0 || i >= len) return r_str("");
    char b[2]; b[0] = buf[i]; b[1] = 0;
    return r_str(b);
}

static script_val_t n_ord(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    int64_t i = arg_num(a, n, 1, 0);
    int64_t len = (int64_t)strlen(buf);
    if (i < 0) i += len;
    if (i < 0 || i >= len) return r_num(0);
    return r_num((unsigned char)buf[i]);
}

static script_val_t n_chr(script_val_t *a, int n) {
    char b[2]; b[0] = (char)(arg_num(a, n, 0, 0) & 0xff); b[1] = 0;
    return r_str(b);
}

static script_val_t n_repeat(script_val_t *a, int n) {
    char s[1024];
    arg_str(a, n, 0, s, sizeof(s));
    int64_t cnt = arg_num(a, n, 1, 0);
    int sl = (int)strlen(s);
    if (cnt <= 0 || sl == 0) return r_str("");
    if (cnt > 4096 / sl) cnt = 4096 / sl;
    int total = sl * (int)cnt;
    char *out = malloc((size_t)total + 1);
    if (!out) return r_str("");
    for (int i = 0; i < (int)cnt; i++)
        for (int j = 0; j < sl; j++) out[i * sl + j] = s[j];
    out[total] = 0;
    return r_owned(out);
}

static script_val_t n_split(script_val_t *a, int n) {
    char s[1024], sep[64];
    arg_str(a, n, 0, s, sizeof(s));
    arg_str(a, n, 1, sep, sizeof(sep));
    if (!sep[0]) return r_str("[]");
    char out[2048];
    int p = 0;
    out[p++] = '[';
    char *q = s;
    int first = 1;
    while (*q) {
        char *hit = strstr(q, sep);
        const char *end = hit ? hit : s + strlen(s);
        if (!first && p < 2000) { out[p++] = ','; out[p++] = ' '; }
        first = 0;
        if (p < 2000) out[p++] = '"';
        for (const char *r = q; r < end && p < 1990; r++) {
            if (*r == '"' || *r == '\\') out[p++] = '\\';
            out[p++] = *r;
        }
        if (p < 2000) out[p++] = '"';
        if (!hit) break;
        q = hit + strlen(sep);
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_join(script_val_t *a, int n) {
    char arr[1024], sep[256];
    arg_str(a, n, 0, arr, sizeof(arr));
    arg_str(a, n, 1, sep, sizeof(sep));
    if (arr[0] != '[' && arr[0] != '{') return r_str(arr);
    int cnt = script_repr_count(arr);
    if (cnt > 64) cnt = 64;
    char out[2048];
    int p = 0;
    for (int i = 0; i < cnt; i++) {
        script_val_t e = script_repr_at(arr, i);
        if (i > 0) for (const char *r = sep; *r && p < 1990;) out[p++] = *r++;
        if (e.type == 1) {
            for (const char *r = e.str ? e.str : ""; *r && p < 1980;) out[p++] = *r++;
        } else {
            char b[24];
            for (const char *r = i2s(e.num, b, sizeof(b)); *r && p < 1980;) out[p++] = *r++;
        }
        if (e.str) free(e.str);
    }
    out[p] = 0;
    return r_str(out);
}

/* ---- collection natives (value semantics: they return a new repr) ---- */

static script_val_t n_at(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    int64_t idx = arg_num(a, n, 1, 0);
    return script_repr_at(arr, idx);
}

static script_val_t n_keys(script_val_t *a, int n) {
    char obj[1024];
    arg_str(a, n, 0, obj, sizeof(obj));
    if (obj[0] != '{') return r_str("[]");
    char out[1024];
    int p = 0;
    out[p++] = '[';
    const char *q = obj;
    int first = 1;
    while (*q && p < 990) {
        if (*q == '"') {
            const char *ks = q + 1;
            const char *ke = ks;
            while (*ke && !(*ke == '"' && ke[-1] != '\\')) ke++;
            const char *t = ke + 1;
            while (*t == ' ') t++;
            if (*t == ':') {
                if (!first && p < 990) { out[p++] = ','; out[p++] = ' '; }
                first = 0;
                out[p++] = '"';
                for (const char *r = ks; r < ke && p < 985; r++) out[p++] = *r;
                out[p++] = '"';
                q = t + 1;
                continue;
            }
            q = ke + 1;
        } else {
            q++;
        }
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_push(script_val_t *a, int n) {
    char arr[1024], val[512];
    arg_str(a, n, 0, arr, sizeof(arr));
    arg_str(a, n, 1, val, sizeof(val));
    if (arr[0] != '[') return r_str(arr);
    int alen = (int)strlen(arr);
    char out[2048];
    int p = 0;
    for (int i = 0; arr[i] && p < 2000; i++) out[p++] = arr[i];
    if (alen >= 2 && out[p-1] == ']') p--;
    while (p > 0 && out[p-1] == ' ') p--;
    if (p > 1) { out[p++] = ','; out[p++] = ' '; }
    if (n >= 2 && a[1].type == 1) {
        out[p++] = '"';
        for (const char *r = val; *r && p < 2000; r++) {
            if (*r == '"' || *r == '\\') out[p++] = '\\';
            out[p++] = *r;
        }
        out[p++] = '"';
    } else {
        for (const char *r = val; *r && p < 2000; r++) out[p++] = *r;
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_pop(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    if (arr[0] != '[') return r_str(arr);
    int cnt = script_repr_count(arr);
    if (cnt <= 1) return r_str("[]");
    int dep = 0, q = 0, last_comma = -1;
    for (int i = 1; arr[i] && arr[i] != ']'; i++) {
        char c = arr[i];
        if (q) { if (c == '"' && arr[i-1] != '\\') q = 0; continue; }
        if (c == '"') q = 1;
        else if (c == '[' || c == '{') dep++;
        else if (c == ']' || c == '}') dep--;
        else if (c == ',' && dep == 0) last_comma = i;
    }
    if (last_comma < 0) return r_str(arr);
    char out[1024];
    int p = 0;
    for (int i = 0; i < last_comma && p < 1000; i++) out[p++] = arr[i];
    while (p > 0 && out[p-1] == ' ') p--;
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_sort(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    if (arr[0] != '[') return r_str(arr);
    int cnt = script_repr_count(arr);
    if (cnt > 64) cnt = 64;
    int64_t vals[64];
    for (int i = 0; i < cnt; i++) {
        script_val_t e = script_repr_at(arr, i);
        vals[i] = e.num;
        if (e.str) free(e.str);
    }
    for (int i = 0; i < cnt - 1; i++)
        for (int j = 0; j < cnt - 1 - i; j++)
            if (vals[j] > vals[j+1]) {
                int64_t t = vals[j]; vals[j] = vals[j+1]; vals[j+1] = t;
            }
    char out[1024];
    int p = 0;
    out[p++] = '[';
    for (int i = 0; i < cnt; i++) {
        if (i > 0) { out[p++] = ','; out[p++] = ' '; }
        char b[24];
        for (const char *r = i2s(vals[i], b, sizeof(b)); *r && p < 990; r++) out[p++] = *r;
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

/* ---- math natives (integer) ---- */

static script_val_t n_abs(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    return r_num(v < 0 ? -v : v);
}
static script_val_t n_min(script_val_t *a, int n) {
    int64_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
    return r_num(x < y ? x : y);
}
static script_val_t n_max(script_val_t *a, int n) {
    int64_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
    return r_num(x > y ? x : y);
}
static script_val_t n_clamp(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0), lo = arg_num(a, n, 1, 0), hi = arg_num(a, n, 2, 0);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return r_num(v);
}
static script_val_t n_pow(script_val_t *a, int n) {
    int64_t b = arg_num(a, n, 0, 0), e = arg_num(a, n, 1, 0);
    int64_t r = 1;
    if (e > 20) e = 20;
    for (int64_t i = 0; i < e; i++) r *= b;
    return r_num(r);
}
static script_val_t n_sqrt(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    if (v < 0) return r_num(0);
    int64_t x = v, y = (v + 1) / 2;
    while (y < x) { x = y; y = (y + v / y) / 2; }
    return r_num(x);
}
static script_val_t n_gcd(script_val_t *a, int n) {
    int64_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y) { int64_t t = x % y; x = y; y = t; }
    return r_num(x);
}
static script_val_t n_lcm(script_val_t *a, int n) {
    int64_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    if (!x || !y) return r_num(0);
    int64_t x0 = x, y0 = y;
    while (y) { int64_t t = x % y; x = y; y = t; }
    if (x && x0 > 9223372036854775807LL / (y0 / x)) return r_num(9223372036854775807LL);
    return r_num(x0 / x * y0);
}
static script_val_t n_idiv(script_val_t *a, int n) {
    int64_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
    return r_num(y ? x / y : 0);
}
static script_val_t n_mod(script_val_t *a, int n) {
    int64_t x = arg_num(a, n, 0, 0), y = arg_num(a, n, 1, 0);
    return r_num(y ? x % y : 0);
}
static script_val_t n_floor(script_val_t *a, int n) { return r_num(arg_num(a, n, 0, 0)); }
static script_val_t n_ceil(script_val_t *a, int n)  { return r_num(arg_num(a, n, 0, 0)); }
static script_val_t n_round(script_val_t *a, int n) { return r_num(arg_num(a, n, 0, 0)); }

static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint32_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}
static script_val_t n_rand(script_val_t *a, int n) {
    int64_t lo = 0, hi = 0;
    if (n >= 1) lo = arg_num(a, n, 0, 0);
    if (n >= 2) hi = arg_num(a, n, 1, 0);
    uint32_t raw = rng_next();
    if (n == 0) return r_num(raw);
    if (hi > lo) return r_num(lo + raw % (uint32_t)(hi - lo));
    if (lo > 0) return r_num(raw % (uint32_t)lo);
    return r_num(raw);
}
static script_val_t n_sum(script_val_t *a, int n) {
    int64_t s = 0;
    for (int i = 0; i < n; i++) s += arg_num(a, n, i, 0);
    return r_num(s);
}
static script_val_t n_cat(script_val_t *a, int n) {
    char out[1024];
    int p = 0;
    for (int i = 0; i < n && p < 990; i++) {
        char tmp[64];
        char *part;
        if (a[i].type == 1) part = a[i].str ? a[i].str : "";
        else part = i2s(a[i].num, tmp, sizeof(tmp));
        for (const char *r = part; *r && p < 990; r++) out[p++] = *r;
    }
    out[p] = 0;
    return r_str(out);
}

/* ---- misc ---- */

static script_val_t n_type(script_val_t *a, int n) {
    if (n < 1) return r_str("null");
    switch (a[0].type) {
    case 0: return r_str("number");
    case 1: return r_str("string");
    case 4: return r_str("function");
    default: return r_str("unknown");
    }
}

static script_val_t n_printn(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    kprintf("%s", buf);
    return r_num(0);
}

static script_val_t n_dump(script_val_t *a, int n) {
    if (n < 1) { kprintf("null\n"); return r_num(0); }
    if (a[0].type == 1) kprintf("'%s'\n", a[0].str ? a[0].str : "");
    else if (a[0].type == 4) kprintf("<function>\n");
    else kprintf("%lld\n", a[0].num);
    return r_num(0);
}

/* ---- extended string natives ---- */

static script_val_t n_padl(script_val_t *a, int n) {
    char s[1024];
    arg_str(a, n, 0, s, sizeof(s));
    int64_t width = arg_num(a, n, 1, 0);
    char ch = ' ';
    if (n >= 3) {
        char tmp[8];
        arg_str(a, n, 2, tmp, sizeof(tmp));
        if (tmp[0]) ch = tmp[0];
    }
    int sl = (int)strlen(s);
    if (width < 0) width = sl;
    if (width > 4096) width = 4096;
    if (sl >= width) return r_str(s);
    int pad = (int)width - sl;
    char *out = malloc((size_t)width + 1);
    if (!out) return r_str(s);
    for (int i = 0; i < pad; i++) out[i] = ch;
    for (int i = 0; i < sl; i++) out[pad + i] = s[i];
    out[width] = 0;
    return r_owned(out);
}

static script_val_t n_padr(script_val_t *a, int n) {
    char s[1024];
    arg_str(a, n, 0, s, sizeof(s));
    int64_t width = arg_num(a, n, 1, 0);
    char ch = ' ';
    if (n >= 3) {
        char tmp[8];
        arg_str(a, n, 2, tmp, sizeof(tmp));
        if (tmp[0]) ch = tmp[0];
    }
    int sl = (int)strlen(s);
    if (width < 0) width = sl;
    if (width > 4096) width = 4096;
    if (sl >= width) return r_str(s);
    char *out = malloc((size_t)width + 1);
    if (!out) return r_str(s);
    for (int i = 0; i < sl; i++) out[i] = s[i];
    for (int i = sl; i < width; i++) out[i] = ch;
    out[width] = 0;
    return r_owned(out);
}

static script_val_t n_countsub(script_val_t *a, int n) {
    char h[1024], nd[256];
    arg_str(a, n, 0, h, sizeof(h));
    arg_str(a, n, 1, nd, sizeof(nd));
    if (!nd[0]) return r_num(0);
    int count = 0;
    char *p = h;
    int nl = (int)strlen(nd);
    while ((p = strstr(p, nd)) != 0) {
        count++;
        p += nl;
    }
    return r_num(count);
}

static script_val_t n_replaceall(script_val_t *a, int n) {
    char s[1024], fr[64], to[64];
    arg_str(a, n, 0, s, sizeof(s));
    arg_str(a, n, 1, fr, sizeof(fr));
    arg_str(a, n, 2, to, sizeof(to));
    if (!fr[0]) return r_str(s);
    int fl = (int)strlen(fr), tl = (int)strlen(to);
    char out[2048];
    int p = 0;
    const char *q = s;
    while (*q && p < 2000) {
        if (strncmp(q, fr, fl) == 0) {
            for (int i = 0; i < tl && p < 1990; i++) out[p++] = to[i];
            q += fl;
        } else {
            out[p++] = *q++;
        }
    }
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_titlecase(script_val_t *a, int n) {
    char buf[1024];
    arg_str(a, n, 0, buf, sizeof(buf));
    int in_word = 0;
    for (char *p = buf; *p; p++) {
        int is_let = (p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z');
        if (is_let && !in_word) {
            if (p[0] >= 'a' && p[0] <= 'z') p[0] = (char)(p[0] - 'a' + 'A');
            in_word = 1;
        } else if (is_let) {
            if (p[0] >= 'A' && p[0] <= 'Z') p[0] = (char)(p[0] - 'A' + 'a');
        } else in_word = 0;
    }
    return r_str(buf);
}

/* ---- extended math natives ---- */

static script_val_t n_isprime(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    if (v < 2) return r_num(0);
    if (v < 4) return r_num(1);
    if (v % 2 == 0) return r_num(0);
    for (int64_t d = 3; d <= v / d; d += 2)
        if (v % d == 0) return r_num(0);
    return r_num(1);
}

static script_val_t n_fact(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    if (v < 0) return r_num(0);
    if (v > 20) v = 20;
    int64_t r = 1;
    for (int64_t i = 2; i <= v; i++) r *= i;
    return r_num(r);
}

static script_val_t n_iseven(script_val_t *a, int n) {
    return r_num((arg_num(a, n, 0, 0) & 1) == 0);
}

static script_val_t n_isodd(script_val_t *a, int n) {
    return r_num((arg_num(a, n, 0, 0) & 1) != 0);
}

static script_val_t n_sign(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    return r_num((v > 0) - (v < 0));
}

static script_val_t n_digits(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    if (v == 0) return r_num(1);
    uint64_t u = (v < 0) ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    int d = 0;
    while (u) { u /= 10; d++; }
    return r_num(d);
}

static const char *hex_chars = "0123456789abcdef";

static script_val_t n_hex(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0);
    uint64_t u = (v < 0) ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    char b[17];
    int p = 16;
    b[16] = 0;
    do { b[--p] = hex_chars[u & 0xF]; u >>= 4; } while (u);
    return r_str(b + p);
}

static script_val_t n_unhex(script_val_t *a, int n) {
    char buf[64];
    arg_str(a, n, 0, buf, sizeof(buf));
    const char *p = buf;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    uint64_t r = 0;
    for (; *p; p++) {
        r <<= 4;
        char c = *p;
        if (c >= '0' && c <= '9') r |= (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') r |= (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') r |= (uint64_t)(c - 'A' + 10);
        else return r_num((int64_t)r);
        if (r & (1ULL << 63)) break;
    }
    return r_num((int64_t)r);
}

static script_val_t n_bitand(script_val_t *a, int n) {
    return r_num(arg_num(a, n, 0, 0) & arg_num(a, n, 1, 0));
}
static script_val_t n_bitor(script_val_t *a, int n) {
    return r_num(arg_num(a, n, 0, 0) | arg_num(a, n, 1, 0));
}
static script_val_t n_bitxor(script_val_t *a, int n) {
    return r_num(arg_num(a, n, 0, 0) ^ arg_num(a, n, 1, 0));
}
static script_val_t n_bnot(script_val_t *a, int n) {
    return r_num(~arg_num(a, n, 0, 0));
}
static script_val_t n_shl(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0), s = arg_num(a, n, 1, 0);
    if (s < 0 || s > 63) return r_num(0);
    return r_num((int64_t)((uint64_t)v << s));
}
static script_val_t n_shr(script_val_t *a, int n) {
    int64_t v = arg_num(a, n, 0, 0), s = arg_num(a, n, 1, 0);
    if (s < 0 || s > 63) return r_num(0);
    return r_num((int64_t)((uint64_t)v >> s));
}

/* ---- extended collection natives ---- */

static script_val_t n_first(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    if (arr[0] != '[') return r_str(arr);
    int cnt = script_repr_count(arr);
    int64_t take = arg_num(a, n, 1, 1);
    if (take > cnt) take = cnt;
    if (take < 0) take = 0;
    char out[2048];
    int p = 0;
    out[p++] = '[';
    for (int64_t i = 0; i < take; i++) {
        script_val_t e = script_repr_at(arr, i);
        if (i > 0 && p < 2000) { out[p++] = ','; out[p++] = ' '; }
        if (e.type == 1) {
            if (p < 2000) out[p++] = '"';
            for (const char *r = e.str ? e.str : ""; *r && p < 1990; r++) {
                if (*r == '"' || *r == '\\') out[p++] = '\\';
                out[p++] = *r;
            }
            if (p < 2000) out[p++] = '"';
        } else {
            char b[24];
            for (const char *r = i2s(e.num, b, sizeof(b)); *r && p < 1990; r++) out[p++] = *r;
        }
        if (e.str) free(e.str);
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_last(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    if (arr[0] != '[') return r_str(arr);
    int cnt = script_repr_count(arr);
    int64_t take = arg_num(a, n, 1, 1);
    if (take > cnt) take = cnt;
    if (take < 0) take = 0;
    char out[2048];
    int p = 0;
    out[p++] = '[';
    for (int64_t i = cnt - take; i < cnt; i++) {
        script_val_t e = script_repr_at(arr, i);
        int64_t pos = i - (cnt - take);
        if (pos > 0 && p < 2000) { out[p++] = ','; out[p++] = ' '; }
        if (e.type == 1) {
            if (p < 2000) out[p++] = '"';
            for (const char *r = e.str ? e.str : ""; *r && p < 1990; r++) {
                if (*r == '"' || *r == '\\') out[p++] = '\\';
                out[p++] = *r;
            }
            if (p < 2000) out[p++] = '"';
        } else {
            char b[24];
            for (const char *r = i2s(e.num, b, sizeof(b)); *r && p < 1990; r++) out[p++] = *r;
        }
        if (e.str) free(e.str);
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_lreverse(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    if (arr[0] != '[') return r_str(arr);
    int cnt = script_repr_count(arr);
    if (cnt > 64) cnt = 64;
    char out[2048];
    int p = 0;
    out[p++] = '[';
    for (int i = cnt - 1; i >= 0; i--) {
        script_val_t e = script_repr_at(arr, i);
        if (i < cnt - 1 && p < 2000) { out[p++] = ','; out[p++] = ' '; }
        if (e.type == 1) {
            if (p < 2000) out[p++] = '"';
            for (const char *r = e.str ? e.str : ""; *r && p < 1990; r++) {
                if (*r == '"' || *r == '\\') out[p++] = '\\';
                out[p++] = *r;
            }
            if (p < 2000) out[p++] = '"';
        } else {
            char b[24];
            for (const char *r = i2s(e.num, b, sizeof(b)); *r && p < 1990; r++) out[p++] = *r;
        }
        if (e.str) free(e.str);
    }
    out[p++] = ']';
    out[p] = 0;
    return r_str(out);
}

static script_val_t n_lcontains(script_val_t *a, int n) {
    char arr[1024];
    arg_str(a, n, 0, arr, sizeof(arr));
    if (arr[0] != '[') return r_num(0);
    int cnt = script_repr_count(arr);
    char val[256];
    arg_str(a, n, 1, val, sizeof(val));
    int val_is_str = (n >= 2 && a[1].type == 1);
    for (int i = 0; i < cnt; i++) {
        script_val_t e = script_repr_at(arr, i);
        int match = 0;
        if (val_is_str) {
            match = (e.type == 1 && e.str && strcmp(e.str, val) == 0);
        } else {
            match = (e.type == 0 && e.num == csl_atoll(val));
        }
        if (e.str) free(e.str);
        if (match) return r_num(1);
    }
    return r_num(0);
}

/* ---- system natives ---- */

static script_val_t n_sleep(script_val_t *a, int n) {
    sched_sleep_ms(arg_num(a, n, 0, 0));
    return r_num(0);
}

static script_val_t n_ticks(script_val_t *a, int n) {
    (void)a; (void)n;
    return r_num((int64_t)timer_get_milliseconds());
}

static script_val_t n_shell(script_val_t *a, int n) {
    char cmd[512];
    arg_str(a, n, 0, cmd, sizeof(cmd));
    if (!cmd[0]) return r_num(-1);
    return r_num(shell_execute(cmd));
}

static script_val_t n_readln(script_val_t *a, int n) {
    (void)a; (void)n;
    char buf[512];
    int r = shell_readln(buf, sizeof(buf));
    if (r < 0) return r_str("");
    return r_str(buf);
}

static script_val_t n_kbhit(script_val_t *a, int n) {
    (void)a; (void)n;
    int c = shell_kbhit();
    if (c <= 0) return r_num(0);
    return r_num(c);
}

/* ---- X11 bridge for csl: canvas-backed kernel windows ---- */

#define CSL_X11_MAX     6
#define CSL_X11_MAX_DIM 512

typedef struct {
    int in_use;
    uint32_t xid;
    int w, h;
    uint32_t *pixels;
} csl_canvas_t;

static csl_canvas_t g_ccanvas[CSL_X11_MAX];

static csl_canvas_t *canvas_find(uint32_t xid) {
    for (int i = 0; i < CSL_X11_MAX; i++)
        if (g_ccanvas[i].in_use && g_ccanvas[i].xid == xid)
            return &g_ccanvas[i];
    return 0;
}

static void canvas_put_px(csl_canvas_t *c, int x, int y, uint32_t px) {
    if (!c || !c->pixels) return;
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    c->pixels[(size_t)y * (size_t)c->w + (size_t)x] = px;
}

/* Accept a color as a number (0xRRGGBB, or 0xAARRGGBB when > 0xFFFFFF) or a
 * string ("#RRGGBB" / "RRGGBB" / "0xRRGGBB" / 8-digit ARGB). */
static uint32_t color_arg(script_val_t *a, int n, int idx, uint32_t dflt) {
    int64_t v = arg_num(a, n, idx, -1);
    if (v >= 0) {
        uint32_t c = (uint32_t)((uint64_t)v & 0xFFFFFFFFULL);
        return (v > 0xFFFFFF) ? c : (0xFF000000u | c);
    }
    char buf[32];
    arg_str(a, n, idx, buf, sizeof(buf));
    const char *p = buf;
    if (*p == '#') p++;
    else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    uint32_t c = 0;
    int digit = 0;
    const char *q = p;
    for (; *q && digit < 8; q++) {
        c <<= 4;
        char h = *q;
        if (h >= '0' && h <= '9') c |= (uint32_t)(h - '0');
        else if (h >= 'a' && h <= 'f') c |= (uint32_t)(h - 'a' + 10);
        else if (h >= 'A' && h <= 'F') c |= (uint32_t)(h - 'A' + 10);
        else break;
        digit++;
    }
    if (digit == 6) return 0xFF000000u | c;
    if (digit == 8) return c;
    return dflt;
}

static script_val_t x11_create_n(script_val_t *a, int n) {
    char title[64];
    arg_str(a, n, 0, title, sizeof(title));
    int w = (int)arg_num(a, n, 1, 480);
    int h = (int)arg_num(a, n, 2, 360);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > CSL_X11_MAX_DIM) w = CSL_X11_MAX_DIM;
    if (h > CSL_X11_MAX_DIM) h = CSL_X11_MAX_DIM;
    if (!title[0]) strcpy(title, "csl");

    int slot = -1;
    for (int i = 0; i < CSL_X11_MAX; i++) {
        if (!g_ccanvas[i].in_use) { slot = i; break; }
    }
    if (slot < 0) return r_num(0);

    uint32_t xid = x11_create_kernel_window(title, w, h);
    if (!xid) return r_num(0);

    uint32_t *px = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
    if (!px) {
        x11_release_window(xid);
        return r_num(0);
    }
    for (int i = 0; i < w * h; i++) px[i] = 0xFF202028;

    g_ccanvas[slot].in_use = 1;
    g_ccanvas[slot].xid = xid;
    g_ccanvas[slot].w = w;
    g_ccanvas[slot].h = h;
    g_ccanvas[slot].pixels = px;
    return r_num((int64_t)xid);
}

static script_val_t x11_destroy_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    csl_canvas_t *c = canvas_find(xid);
    if (c) {
        if (c->pixels) free(c->pixels);
        c->in_use = 0;
    }
    prs_kill_client(xid);
    return r_num(0);
}

static script_val_t x11_show_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_num(0);
    prs_show_client(xid);
    return r_num(1);
}

static script_val_t x11_hide_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_num(0);
    prs_hide_client(xid);
    return r_num(1);
}

static script_val_t x11_focus_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_num(0);
    prs_focus_client(xid);
    return r_num(1);
}

static script_val_t x11_move_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    int x = (int)arg_num(a, n, 1, 0);
    int y = (int)arg_num(a, n, 2, 0);
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_num(0);
    prs_position_client(xid, x, y, w->width, w->height);
    return r_num(1);
}

static script_val_t x11_resize_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    int nw = (int)arg_num(a, n, 1, 0);
    int nh = (int)arg_num(a, n, 2, 0);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if (nw > CSL_X11_MAX_DIM) nw = CSL_X11_MAX_DIM;
    if (nh > CSL_X11_MAX_DIM) nh = CSL_X11_MAX_DIM;
    csl_canvas_t *c = canvas_find(xid);
    x11_window_t *w = x11_get_window(xid);
    if (!c && !w) return r_num(0);
    if (c) {
        uint32_t *np = (uint32_t *)malloc((size_t)nw * (size_t)nh * 4);
        if (!np) return r_num(0);
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                uint32_t px = 0xFF202028;
                if (c->pixels && y < c->h && x < c->w)
                    px = c->pixels[(size_t)y * (size_t)c->w + (size_t)x];
                np[(size_t)y * (size_t)nw + (size_t)x] = px;
            }
        if (c->pixels) free(c->pixels);
        c->pixels = np;
        c->w = nw;
        c->h = nh;
    }
    if (w) {
        int x = w->x, y = w->y;
        prs_position_client(xid, x, y, nw, nh);
    }
    return r_num(1);
}

static script_val_t x11_geom_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_str("[0, 0, 0, 0]");
    char out[64];
    snprintf(out, sizeof(out), "[%d, %d, %d, %d]", w->x, w->y, w->width, w->height);
    return r_str(out);
}

static script_val_t x11_title_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_str("");
    return r_str(w->title);
}

static script_val_t x11_settitle_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    char title[64];
    arg_str(a, n, 1, title, sizeof(title));
    x11_window_t *w = x11_get_window(xid);
    if (!w) return r_num(0);
    prs_set_title(xid, title);
    return r_num(1);
}

static script_val_t x11_atom_n(script_val_t *a, int n) {
    char name[128];
    arg_str(a, n, 0, name, sizeof(name));
    return r_num((int64_t)x11_intern_atom(name, 0));
}

static script_val_t x11_visible_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    x11_window_t *w = x11_get_window(xid);
    return r_num(w && w->mapped ? 1 : 0);
}

static script_val_t x11_blit_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    csl_canvas_t *c = canvas_find(xid);
    if (!c || !c->pixels) return r_num(0);
    prs_blit(xid, 0, 0, c->w, c->h, c->pixels, c->w);
    return r_num(1);
}

static script_val_t x11_clear_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    uint32_t color = color_arg(a, n, 1, 0xFF202028);
    csl_canvas_t *c = canvas_find(xid);
    if (!c || !c->pixels) return r_num(0);
    for (int i = 0; i < c->w * c->h; i++) c->pixels[i] = color;
    return r_num(1);
}

static script_val_t x11_fill_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    int rx = (int)arg_num(a, n, 1, 0);
    int ry = (int)arg_num(a, n, 2, 0);
    int rw = (int)arg_num(a, n, 3, 0);
    int rh = (int)arg_num(a, n, 4, 0);
    uint32_t color = color_arg(a, n, 5, 0xFF202028);
    csl_canvas_t *c = canvas_find(xid);
    if (!c || !c->pixels) return r_num(0);
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = rx + rw, y1 = ry + rh;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            c->pixels[(size_t)y * (size_t)c->w + (size_t)x] = color;
    return r_num(1);
}

static script_val_t x11_pixel_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    int x = (int)arg_num(a, n, 1, 0);
    int y = (int)arg_num(a, n, 2, 0);
    uint32_t color = color_arg(a, n, 3, 0xFF202028);
    csl_canvas_t *c = canvas_find(xid);
    if (!c) return r_num(0);
    canvas_put_px(c, x, y, color);
    return r_num(1);
}

static script_val_t x11_line_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    int x0 = (int)arg_num(a, n, 1, 0);
    int y0 = (int)arg_num(a, n, 2, 0);
    int x1 = (int)arg_num(a, n, 3, 0);
    int y1 = (int)arg_num(a, n, 4, 0);
    uint32_t color = color_arg(a, n, 5, 0xFF202028);
    csl_canvas_t *c = canvas_find(xid);
    if (!c) return r_num(0);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        canvas_put_px(c, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
    return r_num(1);
}

static script_val_t x11_rect_n(script_val_t *a, int n) {
    uint32_t xid = (uint32_t)arg_num(a, n, 0, 0);
    int rx = (int)arg_num(a, n, 1, 0);
    int ry = (int)arg_num(a, n, 2, 0);
    int rw = (int)arg_num(a, n, 3, 0);
    int rh = (int)arg_num(a, n, 4, 0);
    uint32_t color = color_arg(a, n, 5, 0xFF202028);
    csl_canvas_t *c = canvas_find(xid);
    if (!c) return r_num(0);
    if (rw <= 0 || rh <= 0) return r_num(0);
    for (int i = 0; i < rw; i++) {
        canvas_put_px(c, rx + i, ry, color);
        canvas_put_px(c, rx + i, ry + rh - 1, color);
    }
    for (int j = 0; j < rh; j++) {
        canvas_put_px(c, rx, ry + j, color);
        canvas_put_px(c, rx + rw - 1, ry + j, color);
    }
    return r_num(1);
}

/* ---- API ---- */

int csl_register_func(const char *name, script_native_fn fn) {
    return script_register_func(name, fn);
}

int csl_init(void) {
    kprintf("csl: CodeOS Scripting Language initialized\n");

    /* string */
    csl_register_func("str",      n_str);
    csl_register_func("num",      n_num);
    csl_register_func("len",      n_len);
    csl_register_func("upper",    n_upper);
    csl_register_func("lower",    n_lower);
    csl_register_func("trim",     n_trim);
    csl_register_func("reverse",  n_reverse);
    csl_register_func("substr",   n_substr);
    csl_register_func("instr",    n_instr);
    csl_register_func("replace",  n_replace);
    csl_register_func("contains", n_contains);
    csl_register_func("startswith", n_startswith);
    csl_register_func("endswith", n_endswith);
    csl_register_func("chrat",    n_chrat);
    csl_register_func("ord",      n_ord);
    csl_register_func("chr",      n_chr);
    csl_register_func("repeat",   n_repeat);
    csl_register_func("split",    n_split);
    csl_register_func("join",     n_join);

    /* collections */
    csl_register_func("at",       n_at);
    csl_register_func("keys",     n_keys);
    csl_register_func("push",     n_push);
    csl_register_func("pop",      n_pop);
    csl_register_func("sort",     n_sort);

    /* math */
    csl_register_func("abs",      n_abs);
    csl_register_func("min",      n_min);
    csl_register_func("max",      n_max);
    csl_register_func("clamp",    n_clamp);
    csl_register_func("pow",      n_pow);
    csl_register_func("sqrt",     n_sqrt);
    csl_register_func("gcd",      n_gcd);
    csl_register_func("lcm",      n_lcm);
    csl_register_func("idiv",     n_idiv);
    csl_register_func("mod",      n_mod);
    csl_register_func("floor",    n_floor);
    csl_register_func("ceil",     n_ceil);
    csl_register_func("round",    n_round);
    csl_register_func("rand",     n_rand);
    csl_register_func("sum",      n_sum);
    csl_register_func("cat",      n_cat);

    /* misc */
    csl_register_func("type",     n_type);
    csl_register_func("printn",   n_printn);
    csl_register_func("dump",     n_dump);

    /* extended string */
    csl_register_func("padl",      n_padl);
    csl_register_func("padr",      n_padr);
    csl_register_func("countsub",  n_countsub);
    csl_register_func("replaceall", n_replaceall);
    csl_register_func("title",     n_titlecase);

    /* extended math */
    csl_register_func("isprime",   n_isprime);
    csl_register_func("fact",      n_fact);
    csl_register_func("iseven",    n_iseven);
    csl_register_func("isodd",     n_isodd);
    csl_register_func("sign",      n_sign);
    csl_register_func("digits",    n_digits);
    csl_register_func("hex",       n_hex);
    csl_register_func("unhex",     n_unhex);
    csl_register_func("bitand",    n_bitand);
    csl_register_func("bitor",     n_bitor);
    csl_register_func("bitxor",    n_bitxor);
    csl_register_func("bnot",      n_bnot);
    csl_register_func("shl",       n_shl);
    csl_register_func("shr",       n_shr);

    /* extended collections */
    csl_register_func("first",     n_first);
    csl_register_func("last",      n_last);
    csl_register_func("lreverse",  n_lreverse);
    csl_register_func("lcontains", n_lcontains);

    /* system */
    csl_register_func("sleep",     n_sleep);
    csl_register_func("ticks",     n_ticks);
    csl_register_func("shell",     n_shell);
    csl_register_func("readln",    n_readln);
    csl_register_func("kbhit",     n_kbhit);

    /* x11 bridge */
    csl_register_func("x11_win",     x11_create_n);
    csl_register_func("x11_close",   x11_destroy_n);
    csl_register_func("x11_show",    x11_show_n);
    csl_register_func("x11_hide",    x11_hide_n);
    csl_register_func("x11_focus",   x11_focus_n);
    csl_register_func("x11_move",    x11_move_n);
    csl_register_func("x11_resize",  x11_resize_n);
    csl_register_func("x11_geom",    x11_geom_n);
    csl_register_func("x11_title",   x11_title_n);
    csl_register_func("x11_settitle", x11_settitle_n);
    csl_register_func("x11_atom",    x11_atom_n);
    csl_register_func("x11_visible", x11_visible_n);
    csl_register_func("x11_blit",    x11_blit_n);
    csl_register_func("x11_clear",   x11_clear_n);
    csl_register_func("x11_fill",    x11_fill_n);
    csl_register_func("x11_pixel",   x11_pixel_n);
    csl_register_func("x11_line",    x11_line_n);
    csl_register_func("x11_rect",    x11_rect_n);

    return 0;
}

void csl_print(const char *s) {
    kprintf("%s", s ? s : "");
}

void csl_print_num(int64_t n) {
    kprintf("%lld", n);
}

int csl_eval(const char *line) {
    if (!line || !*line) return 0;
    if (line[0] == '#') return 0;
    if (strncmp(line, "//", 2) == 0) return 0;

    script_val_t res;
    int ret = script_eval(line, &res);
    if (ret == 0) {
        if (res.type == 0)
            kprintf("%lld\n", res.num);
        else if (res.str)
            kprintf("%s\n", res.str);
        if (res.str) free(res.str);
    } else {
        int ret2 = shell_execute(line);
        if (ret2 != 0 && ret2 != -1) {
            kprintf("csl: ok (shell)\n");
            ret = 0;
        }
    }
    return ret;
}

int csl_run_file(const char *path) {
    int sz = 0, dir = 0;
    if (fs_get_info(path, &sz, &dir) < 0 || dir || sz <= 0) {
        kprintf("csl: cannot open '%s'\n", path);
        return -1;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { kprintf("csl: OOM\n"); return -1; }

    int n = fs_read(path, buf, sz);
    if (n <= 0) { free(buf); return -1; }
    buf[n] = 0;

    char *line = buf;
    int lineno = 0;
    while (line && *line) {
        char *next = line;
        while (*next && *next != '\n') next++;
        if (*next == '\n') { *next = 0; next++; }
        else { next = 0; }

        if (*line && *line != '#') {
            char *trim = line;
            while (*trim == ' ' || *trim == '\t') trim++;
            if (*trim && strncmp(trim, "//", 2) != 0) {
                if (csl_eval(trim) < 0)
                    kprintf("csl: error at %s:%d\n", path, lineno + 1);
            }
        }
        line = next;
        lineno++;
    }

    free(buf);
    return 0;
}

int csl_set_var(const char *name, csl_val_t val) {
    script_val_t sv;
    sv.type = (val.type == CSL_TYPE_NUM) ? 0 : 1;
    sv.num = val.num;
    sv.str = 0;
    if (val.type == CSL_TYPE_STR) {
        sv.str = malloc(strlen(val.str) + 1);
        if (sv.str) strcpy(sv.str, val.str);
    }
    script_set_var(name, sv);
    return 0;
}

int csl_get_var(const char *name, csl_val_t *val) {
    script_val_t sv;
    if (script_get_var(name, &sv) < 0) return -1;
    memset(val, 0, sizeof(*val));
    val->type = (sv.type == 0) ? CSL_TYPE_NUM : CSL_TYPE_STR;
    val->num = sv.num;
    if (sv.str && val->type == CSL_TYPE_STR)
        strlcpy(val->str, sv.str, sizeof(val->str));
    if (sv.str) free(sv.str);
    return 0;
}