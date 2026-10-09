#include "download.h"
#include "windows.h"
#include "string.h"
#include "net.h"
#include "ext2.h"
#include "fs.h"
#include "kprintf.h"
#include "mm.h"
#include "fb.h"
#include "pixelman.h"
#include "desktop.h"

#define DL_W 500
#define DL_H 320
#define DL_ROW_H 36
#define DL_PAD 12
#define DL_TITLE_H 30
#define DL_STAT_H 28
#define DL_PROGRESS_H 4

static int dl_win;
static int dl_open_flag;
static int dl_scroll;
static dl_job_t dl_jobs[DL_MAX_JOBS];
static int dl_job_count;

void dl_init(void) {
    dl_open_flag = 0;
    dl_scroll = 0;
    dl_job_count = 0;
    for (int i = 0; i < DL_MAX_JOBS; i++) {
        dl_jobs[i].state = DL_IDLE;
        dl_jobs[i].data = NULL;
        dl_jobs[i].url[0] = 0;
        dl_jobs[i].filename[0] = 0;
    }
}

int dl_open(void) {
    if (dl_open_flag) return 1;
    int sw_i = (int)fb_getwidth(), sh_i = (int)fb_getheight();
    int x = (sw_i - DL_W) / 2;
    int y = (sh_i - DL_H) / 2;
    dl_win = window_new(x, y, DL_W, DL_H, "Downloads", C_TEXT, C_BASE);
    if (dl_win < 0) return 0;
    window_t *w = window_get(dl_win);
    w->has_close = 1;
    w->close_x = w->x + w->w - 18;
    w->close_y = w->y + 6;
    dl_open_flag = 1;
    return 1;
}

int dl_is_open(void) { return dl_open_flag; }

void dl_close(void) {
    dl_open_flag = 0;
}

static dl_job_t *dl_find_idle(void) {
    for (int i = 0; i < DL_MAX_JOBS; i++)
        if (dl_jobs[i].state == DL_IDLE) return &dl_jobs[i];
    return NULL;
}

static void sort_jobs(void) {
    int live = 0;
    for (int i = 0; i < DL_MAX_JOBS; i++)
        if (dl_jobs[i].state != DL_IDLE) live++;
    if (live == dl_job_count) return;
    int wp = 0;
    for (int i = 0; i < DL_MAX_JOBS; i++)
        if (dl_jobs[i].state != DL_IDLE) {
            if (wp != i) {
                dl_jobs[wp] = dl_jobs[i];
                dl_jobs[i].state = DL_IDLE;
                dl_jobs[i].data = NULL;
            }
            wp++;
        }
    dl_job_count = wp;
}

static void progress_cb(int received, int total_estimate) {
    (void)total_estimate;
    for (int i = 0; i < DL_MAX_JOBS; i++) {
        if (dl_jobs[i].state == DL_DOWNLOADING) {
            int p = 0;
            if (dl_jobs[i].size > 0)
                p = received * 100 / dl_jobs[i].size;
            else if (total_estimate > 0)
                p = received * 100 / total_estimate;
            else
                p = received * 100 / 65535;
            if (p > 99) p = 99;
            dl_jobs[i].progress = p;
            desktop_redraw();
            break;
        }
    }
}

static int parse_url(const char *url, char *host, int host_max, char *path, int path_max) {
    const char *h = url;
    if (strncmp(h, "http://", 7) == 0) h += 7;
    else if (strncmp(h, "https://", 8) == 0) h += 8;
    else return -1;
    const char *slash = strchr(h, '/');
    if (slash) {
        int len = (int)(slash - h);
        if (len >= host_max) len = host_max - 1;
        for (int i = 0; i < len; i++) host[i] = h[i];
        host[len] = 0;
        int plen = (int)strlen(slash);
        if (plen >= path_max) plen = path_max - 1;
        for (int i = 0; i < plen; i++) path[i] = slash[i];
        path[plen] = 0;
    } else {
        int len = (int)strlen(h);
        if (len >= host_max) len = host_max - 1;
        for (int i = 0; i < len; i++) host[i] = h[i];
        host[len] = 0;
        path[0] = '/';
        path[1] = 0;
    }
    return 0;
}

static const char *extract_filename(const char *url) {
    const char *last_slash = NULL, *p = url;
    while (*p) { if (*p == '/') last_slash = p; p++; }
    if (last_slash && *(last_slash + 1))
        return last_slash + 1;
    return "download.bin";
}

static int ichar(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int str_cmp_ci(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (!a[i]) return -1;
        if (!b[i]) return 1;
        int ca = ichar((unsigned char)a[i]);
        int cb = ichar((unsigned char)b[i]);
        if (ca != cb) return ca - cb;
    }
    return 0;
}

static void extract_header_val(const char *headers, const char *hname, char *out, int out_max) {
    out[0] = 0;
    const char *p = headers;
    int nlen = (int)strlen(hname);
    while (*p) {
        if (str_cmp_ci(p, hname, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            while (*p == ' ' || *p == '\t') p++;
            int oi = 0;
            while (*p && *p != '\r' && *p != '\n' && oi < out_max - 1)
                out[oi++] = *p++;
            out[oi] = 0;
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

static int parse_content_length(const char *headers) {
    char val[32];
    extract_header_val(headers, "Content-Length", val, sizeof(val));
    if (!val[0]) return -1;
    int n = 0;
    const char *v = val;
    while (*v >= '0' && *v <= '9') { n = n * 10 + (*v - '0'); v++; }
    return n;
}

int dl_start(const char *url, const char *filename) {
    if (!url || !*url) return -1;
    if (!dl_open_flag) dl_open();
    sort_jobs();
    dl_job_t *job = dl_find_idle();
    if (!job) return -1;

    strncpy_safe(job->url, url, DL_URL_MAX);
    const char *fn = filename ? filename : extract_filename(url);
    strncpy_safe(job->filename, fn, DL_FILE_MAX);
    job->state = DL_DOWNLOADING;
    job->progress = 0;
    job->size = 0;
    job->data = NULL;
    job->data_len = 0;

    if (dl_job_count < DL_MAX_JOBS) dl_job_count++;

    char host[128], path[256];
    if (parse_url(url, host, sizeof(host), path, sizeof(path)) < 0) {
        job->state = DL_ERROR;
        return -1;
    }

    uint16_t max_buf = 65535;
    char *buf = malloc(max_buf);
    if (!buf) { job->state = DL_ERROR; return -1; }

    int n = http_get_with_progress(host, 80, path, buf, max_buf - 1, progress_cb);
    if (n <= 0) {
        free(buf);
        job->state = DL_ERROR;
        job->progress = 0;
        return -1;
    }
    buf[n] = 0;

    if (strncmp(buf, "HTTP/1.", 7) == 0) {
        int code = 0;
        const char *cp = buf + 9;
        while (*cp >= '0' && *cp <= '9') { code = code * 10 + (*cp - '0'); cp++; }
        if (code >= 400) {
            free(buf);
            job->state = DL_ERROR;
            job->progress = 0;
            return -1;
        }
    }

    char *body = buf;
    int hdr_end = 0;
    for (int i = 0; i < n - 3; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            hdr_end = i + 4;
            break;
        }
    }
    if (hdr_end > 0) {
        buf[hdr_end - 4] = 0;
        int cl = parse_content_length(buf);
        if (cl > 0) job->size = cl;
        body = buf + hdr_end;

        char cd[128];
        extract_header_val(buf, "Content-Disposition", cd, sizeof(cd));
        if (cd[0]) {
            const char *fnp = strstr(cd, "filename=");
            if (fnp) {
                fnp += 9;
                if (*fnp == '"') fnp++;
                int fi = 0;
                while (*fnp && *fnp != '"' && *fnp != ';' && *fnp != ' ' && fi < DL_FILE_MAX - 1)
                    job->filename[fi++] = *fnp++;
                job->filename[fi] = 0;
            }
        }
    }

    int body_len = n - (int)(body - buf);
    job->data_len = body_len;
    job->size = body_len;

    char *save = malloc(body_len + 1);
    if (!save) { free(buf); job->state = DL_ERROR; return -1; }
    for (int i = 0; i < body_len; i++) save[i] = body[i];
    save[body_len] = 0;

    free(buf);
    job->data = save;
    job->state = DL_COMPLETE;
    job->progress = 100;

    {
        char ext2path[256];
        int pi = 0;
        ext2path[pi++] = '/';
        const char *fnp2 = job->filename;
        while (*fnp2 && pi < 254) ext2path[pi++] = *fnp2++;
        ext2path[pi] = 0;
        if (ext2_mounted()) {
            ext2_creat(ext2path);
            int wrote = ext2_write_file_path(ext2path, save, body_len);
            if (wrote >= 0)
                kprintf("dl: saved to ext2: %s (%d bytes)\n", ext2path, body_len);
            else
                kprintf("dl: ext2 write failed for %s\n", ext2path);
        } else if (body_len <= FS_CONTENT_MAX) {
            char fpath[FS_PATH_MAX];
            int fi = 0;
            const char *dd = "/.downloads";
            while (*dd && fi < FS_PATH_MAX - 1) fpath[fi++] = *dd++;
            fpath[fi++] = '/';
            fnp2 = job->filename;
            while (*fnp2 && fi < FS_PATH_MAX - 1) fpath[fi++] = *fnp2++;
            fpath[fi] = 0;
            fs_mkdir("/.downloads");
            fs_mkfile(fpath);
            if (fs_write(fpath, save, body_len) >= 0)
                kprintf("dl: saved to memfs: %s (%d bytes)\n", fpath, body_len);
            else
                kprintf("dl: memfs write failed for %s\n", fpath);
        } else {
            kprintf("dl: %s (%d bytes) kept in memory (too large for memfs, no ext2)\n",
                    job->filename, body_len);
        }
    }
    desktop_redraw();
    return 0;
}

void dl_update(void) {
}

static dl_job_t *dl_job_get(int idx) {
    int c = 0;
    for (int i = 0; i < DL_MAX_JOBS; i++) {
        if (dl_jobs[i].state != DL_IDLE) {
            if (c == idx) return &dl_jobs[i];
            c++;
        }
    }
    return NULL;
}

static void draw_progress_bar(uint32_t *buf, int stride, pm_rect_t clip,
                              int x, int y, int w, int h, int pct, uint32_t color) {
    pm_composite_rect(buf, stride, clip, x, y, w, h, 0xFF45475A);
    if (pct > 0) {
        int fw = (pct * w + 50) / 100;
        if (fw > w) fw = w;
        pm_composite_rect(buf, stride, clip, x, y, fw, h, color);
    }
}

void dl_draw(void) {
    if (!dl_open_flag) return;
    window_t *w = window_get(dl_win);
    if (!w || !w->visible) return;

    uint32_t *buf = fb_get_active_buffer();
    int stride = fb_get_pitch();
    uint32_t scr_w = fb_getwidth(), scr_h = fb_getheight();
    pm_rect_t clip = {0, 0, (int)scr_w, (int)scr_h};
    int wx = w->x, wy = w->y;

    int lh = DL_H - DL_TITLE_H - DL_STAT_H;
    int ly = wy + DL_TITLE_H;

    int max_visible = lh / DL_ROW_H;
    if (max_visible < 1) max_visible = 1;
    if (dl_scroll > dl_job_count - max_visible)
        dl_scroll = dl_job_count - max_visible;
    if (dl_scroll < 0) dl_scroll = 0;

    int shown = 0;
    for (int i = 0; i < DL_MAX_JOBS && shown < max_visible; i++) {
        int idx = dl_scroll + shown;
        dl_job_t *j = dl_job_get(idx);
        if (!j) { shown++; continue; }

        int ry = ly + shown * DL_ROW_H;
        int rx = wx + DL_PAD;

        uint32_t icon_color;
        char icon_char;
        switch (j->state) {
            case DL_DOWNLOADING: icon_color = C_BLUE;  icon_char = '>'; break;
            case DL_COMPLETE:    icon_color = C_GREEN; icon_char = 'v'; break;
            case DL_ERROR:       icon_color = C_RED;   icon_char = 'X'; break;
            default:             icon_color = C_OVERLAY0; icon_char = '?'; break;
        }

        pm_composite_rect(buf, stride, clip, rx, ry, 12, 12, icon_color);
        fb_drawstr_px(rx + 2, ry - 1, (char[]){icon_char, 0}, C_BASE, icon_color);

        fb_drawstr_px(rx + 18, ry, j->filename, C_TEXT, C_BASE);

        char info[48];
        int ii = 0;
        if (j->state == DL_DOWNLOADING) {
            const char *s = "Downloading ";
            while (*s && ii < 46) info[ii++] = *s++;
        } else if (j->state == DL_COMPLETE) {
            const char *s = "Done ";
            while (*s && ii < 46) info[ii++] = *s++;
        } else if (j->state == DL_ERROR) {
            const char *s = "Error";
            while (*s && ii < 46) info[ii++] = *s++;
        }
        if (j->size > 0 && ii < 40) {
            int sz = j->size;
            if (sz > 1024 * 1024) {
                info[ii++] = ' ';
                int mb = sz / (1024 * 1024);
                int frac = (sz % (1024 * 1024)) / 1024;
                if (mb >= 100) { info[ii++] = '0' + mb / 100; mb %= 100; }
                if (mb >= 10) { info[ii++] = '0' + mb / 10; mb %= 10; }
                info[ii++] = '0' + mb;
                info[ii++] = '.';
                info[ii++] = '0' + frac / 100;
                info[ii++] = 'M';
                info[ii++] = 'B';
            } else if (sz > 1024) {
                info[ii++] = ' ';
                int kb = sz / 1024;
                if (kb >= 100) { info[ii++] = '0' + kb / 100; kb %= 100; }
                if (kb >= 10) { info[ii++] = '0' + kb / 10; kb %= 10; }
                info[ii++] = '0' + kb;
                info[ii++] = 'K';
                info[ii++] = 'B';
            } else {
                info[ii++] = ' ';
                info[ii++] = '0' + sz;
                info[ii++] = 'B';
            }
        }
        info[ii] = 0;
        int iw = fb_text_width(info);
        fb_drawstr_px(wx + DL_W - DL_PAD - iw, ry, info, C_OVERLAY0, C_BASE);

        if (j->state == DL_DOWNLOADING || j->state == DL_COMPLETE) {
            int pb_x = rx + 18;
            int pb_y = ry + 16;
            int pb_w = DL_W - DL_PAD * 2 - 18 - 4;
            draw_progress_bar(buf, stride, clip, pb_x, pb_y, pb_w, DL_PROGRESS_H,
                             j->progress, j->state == DL_DOWNLOADING ? C_BLUE : C_GREEN);
        }

        int sep_y = ry + DL_ROW_H - 1;
        pm_composite_rect(buf, stride, clip, rx, sep_y, DL_W - DL_PAD * 2, 1, 0xFF313244);
        shown++;
    }

    int clear_btn_y = wy + DL_H - DL_STAT_H + 4;
    int clear_btn_x = wx + DL_W / 2 - 50;
    pm_composite_fill_rounded_rect(buf, stride, clip,
                                   clear_btn_x, clear_btn_y, 100, 22, 4, 0xFF45475A);
    fb_drawstr_px(clear_btn_x + 50 - fb_text_width("Clear") / 2,
                  clear_btn_y + 4, "Clear", C_TEXT, 0xFF45475A);
}

int dl_click(int mx, int my) {
    if (!dl_open_flag) return 0;
    window_t *w = window_get(dl_win);
    if (!w || !w->visible) return 0;
    int wx = w->x, wy = w->y;

    if (mx < wx || mx >= wx + DL_W || my < wy || my >= wy + DL_H) return 0;

    int clear_btn_y = wy + DL_H - DL_STAT_H + 4;
    int clear_btn_x = wx + DL_W / 2 - 50;
    if (mx >= clear_btn_x && mx < clear_btn_x + 100 &&
        my >= clear_btn_y && my < clear_btn_y + 22) {
        for (int i = 0; i < DL_MAX_JOBS; i++) {
            if (dl_jobs[i].state != DL_IDLE) {
                free(dl_jobs[i].data);
                dl_jobs[i].data = NULL;
                dl_jobs[i].state = DL_IDLE;
                dl_jobs[i].url[0] = 0;
                dl_jobs[i].filename[0] = 0;
            }
        }
        dl_job_count = 0;
        dl_scroll = 0;
        desktop_redraw();
        return 1;
    }

    int lh = DL_H - DL_TITLE_H - DL_STAT_H;
    int ly = wy + DL_TITLE_H;
    int max_visible = lh / DL_ROW_H;
    if (max_visible < 1) max_visible = 1;

    int shown = 0;
    for (int i = 0; i < DL_MAX_JOBS && shown < max_visible; i++) {
        int idx = dl_scroll + shown;
        dl_job_t *j = dl_job_get(idx);
        if (!j) { shown++; continue; }
        int ry = ly + shown * DL_ROW_H;
        if (mx >= wx + DL_PAD && mx < wx + DL_W - DL_PAD &&
            my >= ry && my < ry + DL_ROW_H) {
            return 1;
        }
        shown++;
    }
    return 1;
}

int dl_key(int key) {
    if (!dl_open_flag) return 0;
    (void)key;
    return 1;
}
