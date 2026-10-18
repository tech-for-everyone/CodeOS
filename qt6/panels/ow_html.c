/* ow_html.c — OpenWeb's shared render output structures + image services.
 *
 * The HTML renderer itself lives in Rust:
 *   kernel/kernel/rust_ow/src/ow_render.rs  →  ow_render_rs()
 * The form API and its persistent per-control edit cache also live in Rust:
 *   kernel/kernel/rust_ow/src/ow_form.rs    →  ow_field_set_value/toggle/at,
 *                                              ow_form_build_query,
 *                                              ow_fv_restore/store
 * Both write the C-owned globals declared in ow_html.h (ow_txt, ow_links,
 * ow_images, ow_forms, ow_form_fields, ow_page_title, ...) via extern statics,
 * exactly like the old C render_html() did — so the Qt frontend is unchanged.
 *
 * What remains here in C:
 *   - the output globals + line/form type definitions (the ow_html.h contract)
 *   - the parallel image fetch workers + ow_image_download()
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
/* <meta name="description"> / og:description text ("" if none). */
char ow_page_desc[OW_URL_MAX];

ow_form_t ow_forms[OW_MAX_FORMS];
int  ow_form_cnt;
ow_form_field_t ow_form_fields[OW_MAX_FIELDS];
int  ow_field_cnt;

/* The form API (ow_field_set_value/toggle/at, ow_form_build_query) and the
 * persistent per-control edit cache (ow_fv_restore/ow_fv_store) now live in
 * Rust: kernel/kernel/rust_ow/src/ow_form.rs. The globals above stay here
 * because they are the shared contract with the Qt frontend. */

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
