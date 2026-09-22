/* ow_html.c — OpenWeb's shared render output structures + form/image services.
 *
 * The HTML renderer itself now lives in Rust:
 *   kernel/kernel/rust_ow/src/ow_render.rs  →  ow_render_rs()
 * It writes the C-owned globals declared in ow_html.h (ow_txt, ow_links,
 * ow_images, ow_forms, ow_form_fields, ow_page_title, ...) via extern statics,
 * exactly like the old C render_html() did — so the Qt frontend is unchanged.
 *
 * What remains here in C:
 *   - the output globals + line/form types (ow_html.h contract)
 *   - the parallel image fetch workers + ow_image_download()
 *   - the form-field API the Qt frontend calls (set/toggle/hit-test/query)
 *   - the persistent per-control edit cache, bridged to Rust via
 *     ow_fv_restore() / ow_fv_store()
 */

#include "ow_html.h"
#include "string.h"
#include "kprintf.h"
#include "mm.h"
#include "net.h"

/* kernel HTTP(S) clients (declared in net.c) */
extern int https_get(const char *host, uint16_t port, const char *path, void *buf, uint16_t max_len);

/* ── Parallel image fetch infrastructure ── */
#include "spinlock.h"

enum {
    IMG_ST_EMPTY   = 0,
    IMG_ST_PENDING = 1,
    IMG_ST_FETCH   = 2,
    IMG_ST_READY   = 3,
    IMG_ST_FAIL    = 4,
};

static volatile int  s_img_state[OW_MAX_IMAGES];
static volatile int  s_img_raw_len[OW_MAX_IMAGES];
static unsigned char s_img_raw[OW_MAX_IMAGES][65535];
static char          s_img_absurl[OW_MAX_IMAGES][OW_URL_MAX];
static spinlock_t    s_img_lock = SPINLOCK_INIT;
static volatile int  s_img_workers_started;

int sched_create_thread(const char *name, void (*entry)(void));
void sched_sleep_ms(uint64_t ms);

static void ow_img_worker(void) {
    for (;;) {
        int idx = -1;
        char url_local[OW_URL_MAX];
        spin_lock(&s_img_lock);
        for (int i = 0; i < OW_MAX_IMAGES; i++) {
            if (s_img_state[i] == IMG_ST_PENDING) {
                s_img_state[i] = IMG_ST_FETCH;
                idx = i;
                int u = 0;
                while (u < OW_URL_MAX - 1 && s_img_absurl[i][u]) {
                    url_local[u] = s_img_absurl[i][u];
                    u++;
                }
                url_local[u] = 0;
                break;
            }
        }
        spin_unlock(&s_img_lock);

        if (idx < 0) { sched_sleep_ms(40); continue; }

        int n = ow_image_download(url_local, s_img_raw[idx], 65535);
        spin_lock(&s_img_lock);
        if (s_img_state[idx] == IMG_ST_FETCH) {
            s_img_raw_len[idx] = (n > 8) ? n : 0;
            s_img_state[idx]   = (n > 8) ? IMG_ST_READY : IMG_ST_FAIL;
        }
        spin_unlock(&s_img_lock);
    }
}

void ow_image_start_workers(void) {
    if (__sync_bool_compare_and_swap(&s_img_workers_started, 0, 1)) {
        sched_create_thread("ow-img-0", ow_img_worker);
        sched_create_thread("ow-img-1", ow_img_worker);
    }
}

void ow_image_enqueue(int idx, const char *abs_url) {
    if (idx < 0 || idx >= OW_MAX_IMAGES || !abs_url) return;
    spin_lock(&s_img_lock);
    if (s_img_state[idx] == IMG_ST_EMPTY || s_img_state[idx] == IMG_ST_FAIL) {
        s_img_state[idx] = IMG_ST_PENDING;
        int u = 0;
        while (u < OW_URL_MAX - 1 && abs_url[u]) { s_img_absurl[idx][u] = abs_url[u]; u++; }
        s_img_absurl[idx][u] = 0;
    }
    spin_unlock(&s_img_lock);
}

int ow_image_state(int idx) {
    if (idx < 0 || idx >= OW_MAX_IMAGES) return IMG_ST_EMPTY;
    return s_img_state[idx];
}

const unsigned char *ow_image_raw(int idx) {
    if (idx < 0 || idx >= OW_MAX_IMAGES) return 0;
    return s_img_raw[idx];
}

int ow_image_raw_len(int idx) {
    if (idx < 0 || idx >= OW_MAX_IMAGES) return 0;
    return s_img_raw_len[idx];
}

void ow_image_reset_all(void) {
    spin_lock(&s_img_lock);
    for (int i = 0; i < OW_MAX_IMAGES; i++) {
        s_img_state[i]   = IMG_ST_EMPTY;
        s_img_raw_len[i] = 0;
        s_img_absurl[i][0] = 0;
    }
    spin_unlock(&s_img_lock);
}

/* ── render output (filled by the Rust renderer) ── */

char ow_txt[OW_TXT_LINES][OW_TXT_COLS];
int  ow_txt_lines;
ow_link_t ow_links[OW_MAX_LINKS];
int  ow_link_cnt;
ow_image_t ow_images[OW_MAX_IMAGES];
int  ow_image_cnt;
int  ow_need_render;
ow_line_info_t ow_line_info[OW_TXT_LINES];
int  ow_line_img[OW_TXT_LINES];
char ow_page_title[OW_URL_MAX];

ow_form_t ow_forms[OW_MAX_FORMS];
int  ow_form_cnt;
ow_form_field_t ow_form_fields[OW_MAX_FIELDS];
int  ow_field_cnt;

/* Persistent per-control edit cache (keys name+type+form action). The renderer
 * is rebuilt on every paint, so user-typed values live here and are re-applied
 * to the matching new field on the next render. The Rust renderer calls the
 * ow_fv_restore()/ow_fv_store() bridges below instead of touching these. */
static char   s_fv_name[OW_MAX_FIELDS][OW_URL_MAX];
static uint8_t s_fv_type[OW_MAX_FIELDS];
static char   s_fv_action[OW_MAX_FIELDS][OW_URL_MAX];
static char   s_fv_value[OW_MAX_FIELDS][OW_URL_MAX];
static uint8_t s_fv_checked[OW_MAX_FIELDS];
static uint8_t s_fv_sel[OW_MAX_FIELDS];
static uint8_t s_fv_edited[OW_MAX_FIELDS];

static void ow_strncpy_n(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Find the form a field belongs to (or -1). */
static int field_form_idx(int fi) {
    for (int k = 0; k < ow_form_cnt; k++)
        if (fi >= ow_forms[k].field_start &&
            fi < ow_forms[k].field_start + ow_forms[k].field_count)
            return k;
    return -1;
}

/* The enclosing form's action (for the persistent cache key). */
static const char *field_form_action(int fi) {
    int k = field_form_idx(fi);
    return (k >= 0) ? ow_forms[k].action : "";
}

/* Apply a cached user edit to a freshly parsed field, if the field identity
 * (form action, name, type) matches the cache slot. */
static void fv_restore(ow_form_field_t *f, int fi) {
    if (fi < 0 || fi >= OW_MAX_FIELDS) return;
    if (!s_fv_edited[fi]) { s_fv_name[fi][0] = 0; return; }
    if (strcmp(s_fv_name[fi], f->name) != 0 || s_fv_type[fi] != f->type ||
        strcmp(s_fv_action[fi], field_form_action(fi)) != 0) {
        s_fv_edited[fi] = 0;
        s_fv_name[fi][0] = 0;
        return;
    }
    if (f->type == OW_FT_CHECKBOX || f->type == OW_FT_RADIO) {
        f->checked = s_fv_checked[fi] ? 1 : 0;
        if (f->type == OW_FT_RADIO && f->checked)
            ow_strncpy_n(f->value, s_fv_value[fi], OW_URL_MAX);
    } else if (f->type == OW_FT_SELECT) {
        f->value[0] = 0;
        if (s_fv_sel[fi] < f->opt_cnt) {
            f->opt_sel = s_fv_sel[fi];
            ow_strncpy_n(f->value, f->opts[f->opt_sel], OW_URL_MAX);
        }
    } else {
        ow_strncpy_n(f->value, s_fv_value[fi], OW_URL_MAX);
    }
}

/* Persist the field's current identity + values into the cache slot. */
static void fv_store(const ow_form_field_t *f, int fi) {
    if (fi < 0 || fi >= OW_MAX_FIELDS) return;
    ow_strncpy_n(s_fv_name[fi], f->name, OW_URL_MAX);
    s_fv_type[fi] = f->type;
    ow_strncpy_n(s_fv_action[fi], field_form_action(fi), OW_URL_MAX);
    if (f->type != OW_FT_CHECKBOX)
        ow_strncpy_n(s_fv_value[fi], f->value, OW_URL_MAX);
    if (s_fv_edited[fi]) {
        if (f->type == OW_FT_CHECKBOX) s_fv_checked[fi] = f->checked ? 1 : 0;
    } else {
        s_fv_checked[fi] = f->checked ? 1 : 0;
    }
    s_fv_sel[fi] = f->opt_sel;
}

/* ── Rust renderer bridge ──
 * Called by ow_render_rs() (ow_render.rs) around each control it lays out. */

void ow_fv_restore(ow_form_field_t *f, int fi) { fv_restore(f, fi); }
void ow_fv_store(const ow_form_field_t *f, int fi) { fv_store(f, fi); }

/* ── exported form API ── */

void ow_field_set_value(int fi, const char *v) {
    ow_form_field_t *f;
    if (fi < 0 || fi >= ow_field_cnt) return;
    f = &ow_form_fields[fi];
    if (!v) v = "";
    if (f->type == OW_FT_SELECT) {
        uint8_t k;
        for (k = 0; k < f->opt_cnt; k++) {
            if (strcmp(f->opts[k], v) == 0) {
                f->opt_sel = k;
                ow_strncpy_n(f->value, f->opts[k], OW_URL_MAX);
                s_fv_edited[fi] = 1;
                s_fv_sel[fi] = k;
                return;
            }
        }
        f->opt_sel = (f->opt_cnt) ? (uint8_t)(f->opt_cnt - 1) : 0;
        return;
    }
    ow_strncpy_n(f->value, v, OW_URL_MAX);
    s_fv_edited[fi] = 1;
    ow_strncpy_n(s_fv_value[fi], v, OW_URL_MAX);
}

void ow_field_toggle(int fi) {
    ow_form_field_t *f;
    if (fi < 0 || fi >= ow_field_cnt) return;
    f = &ow_form_fields[fi];
    if (f->type == OW_FT_CHECKBOX) {
        f->checked = f->checked ? 0 : 1;
        s_fv_edited[fi] = 1;
        s_fv_checked[fi] = f->checked ? 1 : 0;
    } else if (f->type == OW_FT_RADIO) {
        int form = f->form;
        int k;
        for (k = 0; k < ow_field_cnt; k++) {
            ow_form_field_t *r = &ow_form_fields[k];
            if (r->type == OW_FT_RADIO && r->form == form &&
                strcmp(r->name, f->name) == 0) {
                if (k == fi) {
                    r->checked = 1;
                    s_fv_checked[k] = 1;
                    s_fv_edited[k] = 1;
                    ow_strncpy_n(s_fv_value[k], r->value, OW_URL_MAX);
                    s_fv_edited[fi] = 1;
                } else {
                    r->checked = 0;
                    s_fv_checked[k] = 0;
                }
            }
        }
    }
}

int ow_field_at(int line, int col) {
    int k;
    if (line < 0) return -1;
    for (k = 0; k < ow_field_cnt; k++) {
        ow_form_field_t *f = &ow_form_fields[k];
        if (f->line == line && col >= f->col && col < f->col + f->width)
            return k;
    }
    return -1;
}

/* URL-encode one name/value pair into out; returns bytes written. */
static int qpart(char *out, int out_max, const char *name, const char *val) {
    static const char hex[] = "0123456789ABCDEF";
    static const char unres[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    int i = 0, n = 0;
    const char *s;
    for (s = name; *s; s++) {
        if (n + 3 >= out_max) return n;
        if (strchr(unres, *s)) out[n++] = *s;
        else { out[n++] = '%'; out[n++] = hex[((unsigned char)*s) >> 4]; out[n++] = hex[((unsigned char)*s) & 15]; }
    }
    if (n + 1 >= out_max) return n;
    out[n++] = '=';
    for (s = val; *s; s++) {
        if (n + 3 >= out_max) return n;
        if (*s == ' ') out[n++] = '+';
        else if (strchr(unres, *s)) out[n++] = *s;
        else { out[n++] = '%'; out[n++] = hex[((unsigned char)*s) >> 4]; out[n++] = hex[((unsigned char)*s) & 15]; }
    }
    (void)i;
    return n;
}

int ow_form_build_query(const ow_form_t *f, char *out, int out_max) {
    int n = 0, k;
    if (!f || !out || out_max <= 0) return 0;
    out[0] = 0;
    for (k = f->field_start; k < f->field_start + f->field_count && k < ow_field_cnt; k++) {
        ow_form_field_t *fd = &ow_form_fields[k];
        if (fd->type == OW_FT_SUBMIT || fd->type == OW_FT_BUTTON) continue;
        if ((fd->type == OW_FT_CHECKBOX || fd->type == OW_FT_RADIO) && !fd->checked) continue;
        if (!fd->name[0]) continue;
        if (n) { if (n + 1 >= out_max) break; out[n++] = '&'; }
        {
            const char *qv = fd->value;
            if ((fd->type == OW_FT_CHECKBOX || fd->type == OW_FT_RADIO) && !qv[0]) qv = "on";
            n += qpart(out + n, out_max - n, fd->name, qv);
        }
    }
    out[n] = 0;
    return n;
}

/* ── image fetch ── */

static int find_slash(const char *s) {
    int i = 0;
    while (s[i] && s[i] != '/') i++;
    return i;
}

/* Fetch an image over plain HTTP(S). `url` must be absolute
 * (http[s]://host[:port]/path). Returns the decoded body length (>0) or -1.
 * The HTTP header frame is stripped; max_len bounds the whole response. */
int ow_image_download(const char *url, void *buf, int max_len) {
    char host[129], path[600];
    if (!url || !url[0] || !buf || max_len <= 8) return -1;

    int https = (strncmp(url, "https://", 8) == 0);
    const char *rest = url;
    if (strncmp(rest, "http://", 7) == 0) rest += 7;
    else if (strncmp(rest, "https://", 8) == 0) rest += 8;
    else return -1;

    int sl = find_slash(rest);
    int hl = sl;
    if (hl > 128) hl = 128;
    for (int i = 0; i < hl; i++) host[i] = rest[i];
    host[hl] = 0;

    uint16_t port = https ? 443 : 80;
    char *colon = strchr(host, ':');
    if (colon) {
        unsigned long p = 0;
        int ok = 1;
        const char *ps = colon + 1;
        for (; *ps; ps++) {
            if (*ps < '0' || *ps > '9') { ok = 0; break; }
            p = p * 10 + (unsigned long)(*ps - '0');
        }
        if (ok) port = (uint16_t)p;
        *colon = 0;
    }

    if (rest[sl]) {
        int pl = 0;
        for (int i = sl; rest[i] && i < sl + 590; i++) path[pl++] = rest[i];
        path[pl] = 0;
    } else {
        path[0] = '/'; path[1] = 0;
    }

    int cap = (max_len < 65535) ? max_len : 65535;
    int n = https ? https_get(host, port, path, buf, (uint16_t)cap)
                  : http_get(host, port, path, buf, (uint16_t)cap);
    if (n <= 0) return -1;

    /* Strip the header frame (first \r\n\r\n). */
    unsigned char *b = (unsigned char *)buf;
    for (int i = 0; i + 3 < n; i++) {
        if (b[i] == '\r' && b[i+1] == '\n' && b[i+2] == '\r' && b[i+3] == '\n') {
            int body = n - (i + 4);
            memmove(b, b + i + 4, (size_t)body);
            return body;
        }
    }
    /* No header found — treat response as raw body. */
    return n;
}
