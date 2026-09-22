#include "svg.h"
#include "string.h"
#include "mm.h"

/* Integer-only SVG renderer. Coordinates are fixed-point Q16.8
   (multiply actual values by 256) for sub-pixel precision. */

#define FP_SHIFT 8
#define FP_SCALE 256
#define FP_1     (1 << FP_SHIFT)

static int fp_mul(int a, int b) { return (a * b) >> FP_SHIFT; }

/* ---- coordinate (int = Q16.8 fixed point) ---- */
static int to_fp(int v) { return v * FP_SCALE; }

/* ---- number parsing (returns Q16.8) ---- */
static void ski_ws(const char **p, const char *end) {
    while (*p < end && (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r')) (*p)++;
}

static int prs_num(const char **p, const char *end) {
    ski_ws(p, end);
    const char *s = *p;
    if (*p < end && (**p == '-' || **p == '+')) (*p)++;
    while (*p < end && ((**p >= '0' && **p <= '9') || **p == '.')) (*p)++;
    if (*p == s) return 0;
    /* parse integer part */
    int sign = 1, ip = 0, fp = 0, fpd = 1, saw_dot = 0;
    const char *c = s;
    if (*c == '-') { sign = -1; c++; } else if (*c == '+') c++;
    while (c < *p) {
        if (*c == '.') { saw_dot = 1; c++; continue; }
        if (!saw_dot) ip = ip * 10 + (*c - '0');
        else { fp = fp * 10 + (*c - '0'); fpd *= 10; }
        c++;
    }
    int result = ip * FP_SCALE;
    if (saw_dot && fpd > 0) result += fp * FP_SCALE / fpd;
    return sign < 0 ? -result : result;
}

static uint32_t prs_hex(const char **p, const char *end) {
    uint32_t v = 0;
    while (*p < end) {
        int c = **p;
        if (c >= '0' && c <= '9') v = (v << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (c - 'A' + 10);
        else break;
        (*p)++;
    }
    return v;
}

static uint32_t prs_col(const char **p, const char *end) {
    ski_ws(p, end);
    if (*p >= end) return 0xFFFFFFFF;
    if (**p == '#') {
        (*p)++;
        int r = prs_hex(p, end);
        int g = prs_hex(p, end);
        int b = prs_hex(p, end);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    while (*p < end && **p != '>' && **p != '"') (*p)++;
    return 0xFFFFFFFF;
}

/* ---- poly builder ---- */

static void ppush(svg_poly_t *po, int x, int y) {
    if (po->count >= po->cap) {
        po->cap = po->cap ? po->cap * 2 : 128;
        svg_pt_t *np = malloc(po->cap * sizeof(svg_pt_t));
        if (!np) return;
        if (po->pts) {
            for (int i = 0; i < po->count; i++) np[i] = po->pts[i];
            free(po->pts);
        }
        po->pts = np;
    }
    po->pts[po->count].x = x;
    po->pts[po->count].y = y;
    po->count++;
}

/* ---- flatten cubic bezier (all in Q16.8) ---- */

static void fcubic(svg_poly_t *po, int x0, int y0, int x1, int y1,
                   int x2, int y2, int x3, int y3, int d) {
    if (d > 10) { ppush(po, x3, y3); return; }
    int dx = x3 - x0, dy = y3 - y0;
    int err = (dx*(y1-y0) - dy*(x1-x0)) * (dx*(y1-y0) - dy*(x1-x0))
            + (dx*(y2-y0) - dy*(x2-x0)) * (dx*(y2-y0) - dy*(x2-x0));
    if (err < FP_SCALE * FP_SCALE && d > 3) { ppush(po, x3, y3); return; }
    int hx1 = (x0+x1)/2, hy1 = (y0+y1)/2;
    int hx2 = (x1+x2)/2, hy2 = (y1+y2)/2;
    int hx3 = (x2+x3)/2, hy3 = (y2+y3)/2;
    int hx12 = (hx1+hx2)/2, hy12 = (hy1+hy2)/2;
    int hx23 = (hx2+hx3)/2, hy23 = (hy2+hy3)/2;
    int hx = (hx12+hx23)/2, hy = (hy12+hy23)/2;
    fcubic(po, x0,y0, hx1,hy1, hx12,hy12, hx,hy, d+1);
    fcubic(po, hx,hy, hx23,hy23, hx3,hy3, x3,y3, d+1);
}

/* ---- parse SVG path 'd' attribute ---- */

static void prs_path(const char *d, int dlen, svg_poly_t *po) {
    const char *p = d, *end = d + dlen;
    int cx = 0, cy = 0, sx = 0, sy = 0;
    char last = 0;
    int sub_start = 0;
    int closed = 0;
    while (p < end) {
        ski_ws(&p, end);
        if (p >= end) break;
        char cmd = *p;
        if ((cmd >= 'A' && cmd <= 'Z') || (cmd >= 'a' && cmd <= 'z')) { p++; last = cmd; }
        else { cmd = last; if (!cmd) break; }
        int rel = (cmd >= 'a' && cmd <= 'z');
        switch (cmd) {
            case 'M': case 'm': {
                if (po->count > sub_start + 2 && !closed) {
                    ppush(po, po->pts[sub_start].x, po->pts[sub_start].y);
                }
                int x = prs_num(&p, end) + (rel ? cx : 0);
                int y = prs_num(&p, end) + (rel ? cy : 0);
                ppush(po, x, y);
                cx = sx = x; cy = sy = y;
                sub_start = po->count - 1;
                closed = 0;
                break;
            }
            case 'L': case 'l': {
                int x = prs_num(&p, end) + (rel ? cx : 0);
                int y = prs_num(&p, end) + (rel ? cy : 0);
                ppush(po, x, y); cx = x; cy = y;
                break;
            }
            case 'H': case 'h': {
                int x = prs_num(&p, end) + (rel ? cx : 0);
                ppush(po, x, cy); cx = x;
                break;
            }
            case 'V': case 'v': {
                int y = prs_num(&p, end) + (rel ? cy : 0);
                ppush(po, cx, y); cy = y;
                break;
            }
            case 'C': case 'c': {
                int x1 = prs_num(&p, end) + (rel ? cx : 0);
                int y1 = prs_num(&p, end) + (rel ? cy : 0);
                int x2 = prs_num(&p, end) + (rel ? cx : 0);
                int y2 = prs_num(&p, end) + (rel ? cy : 0);
                int x3 = prs_num(&p, end) + (rel ? cx : 0);
                int y3 = prs_num(&p, end) + (rel ? cy : 0);
                fcubic(po, cx, cy, x1, y1, x2, y2, x3, y3, 0);
                cx = x3; cy = y3;
                break;
            }
            case 'Q': case 'q': {
                int x1 = prs_num(&p, end) + (rel ? cx : 0);
                int y1 = prs_num(&p, end) + (rel ? cy : 0);
                int x2 = prs_num(&p, end) + (rel ? cx : 0);
                int y2 = prs_num(&p, end) + (rel ? cy : 0);
                fcubic(po, cx, cy, (cx+2*x1)/3, (cy+2*y1)/3,
                       (2*x1+x2)/3, (2*y1+y2)/3, x2, y2, 0);
                cx = x2; cy = y2;
                break;
            }
            case 'Z': case 'z':
                ppush(po, sx, sy); cx = sx; cy = sy; closed = 1; break;
            default: return;
        }
    }
    if (po->count > sub_start + 2 && !closed) {
        ppush(po, po->pts[sub_start].x, po->pts[sub_start].y);
    }
}

/* ---- scanline fill (integer) ---- */

static void fill_poly(svg_poly_t *po, uint32_t *buf, int w, int h,
                      int sx, int sy, int sw, int sh, uint32_t col) {
    if (!po || po->count < 4) return;
    if (!po->pts || w <= 0 || h <= 0 || !buf) return;
    int ne = 0;
    typedef struct { int y0, y1, x, m; } edg_t;
    edg_t ed[256];
    for (int i = 0; i < po->count - 1 && ne < 256; i++) {
        int x1 = po->pts[i].x, y1 = po->pts[i].y;
        int x2 = po->pts[i+1].x, y2 = po->pts[i+1].y;
        if (y1 == y2) continue;
        if (y1 > y2) { int t; t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }
        ed[ne].y0 = y1; ed[ne].y1 = y2;
        ed[ne].x = x1;
        /* m = dx/dy in Q16.8 fixed point */
        ed[ne].m = (y2 - y1) ? ((x2 - x1) * FP_SCALE) / (y2 - y1) : 0;
        ne++;
    }
    if (ne < 3) return;
    /* sort by y0 */
    for (int i = 1; i < ne; i++)
        for (int j = i; j > 0 && ed[j-1].y0 > ed[j].y0; j--) {
            edg_t t = ed[j]; ed[j] = ed[j-1]; ed[j-1] = t;
        }
    /* sw/sh are Q16.8 from prs_num; convert to pixel-units for scale */
    int sw_px = sw ? (sw / FP_SCALE) : 100;
    int sh_px = sh ? (sh / FP_SCALE) : 100;
    int scx = (w * FP_SCALE) / sw_px;
    int scy = (h * FP_SCALE) / sh_px;
    if (scx < 1) scx = 1;
    if (scy < 1) scy = 1;
    int ox = -sx * scx / FP_SCALE;
    int oy = -sy * scy / FP_SCALE;

    for (int y = 0; y < h; y++) {
        int fy = (y * FP_SCALE - oy) * FP_SCALE / scy;
        int xs[1024], xc = 0;
        for (int i = 0; i < ne; i++)
            if (ed[i].y0 <= fy && ed[i].y1 > fy && xc < 1024)
                xs[xc++] = ed[i].x + fp_mul(ed[i].m, fy - ed[i].y0);
        /* sort xs */
        for (int i = 1; i < xc; i++)
            for (int j = i; j > 0 && xs[j-1] > xs[j]; j--) {
                int t = xs[j]; xs[j] = xs[j-1]; xs[j-1] = t;
            }
        for (int i = 0; i + 1 < xc; i += 2) {
            int x0 = (xs[i] * scx / FP_SCALE + ox + FP_SCALE/2) >> FP_SHIFT;
            int x1 = (xs[i+1] * scx / FP_SCALE + ox + FP_SCALE/2) >> FP_SHIFT;
            if (x0 < 0) x0 = 0;
            if (x1 > w) x1 = w;
            for (int x = x0; x < x1; x++)
                buf[y * w + x] = col;
        }
    }
}

/* ---- SVG attribute finder ---- */
static const char *find_attr(const char *p, const char *end, const char *name, int *vlen) {
    while (p < end) {
        ski_ws(&p, end);
        if (p >= end) break;
        const char *nk = p;
        while (p < end && *p != '=' && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '>')) p++;
        int nlen = (int)(p - nk);
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (p < end && *p == '=') {
            p++;
            ski_ws(&p, end);
            if (p < end) {
                char q = *p;
                if (q == '"' || q == '\'') p++;
                const char *vs = p;
                while (p < end && *p != q) p++;
                *vlen = (int)(p - vs);
                int match = 1;
                const char *nn = name;
                for (int i = 0; i < nlen; i++) {
                    if (!*nn || nk[i] != *nn) { match = 0; break; }
                    nn++;
                }
                if (match && !*nn) {
                    if (q == '"' || q == '\'') p++;
                    return vs;
                }
                if (q == '"' || q == '\'') p++;
            }
        }
    }
    return 0;
}

/* ---- main parse ---- */

svg_doc_t *svg_parse(const char *xml, int len) {
    svg_doc_t *doc = malloc(sizeof(svg_doc_t));
    if (!doc) return 0;
    memset(doc, 0, sizeof(*doc));

    const char *p = xml, *end = xml + len;
    int vbx = 0, vby = 0, vbw = to_fp(100), vbh = to_fp(100);

    while (p < end) {
        while (p < end && *p != '<') p++;
        if (p >= end) break;
        p++;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (p >= end) break;
        const char *tag = p;
        while (p < end && *p != '>' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        int tlen = (int)(p - tag);
        const char *atts_end = p;
        while (atts_end < end && *atts_end != '>') atts_end++;
        if (atts_end >= end) break;
        p = atts_end + 1;

        if (tlen == 3 && tag[0] == 's' && tag[1] == 'v' && tag[2] == 'g') {
            int vlen = 0;
            const char *vb = find_attr(tag, atts_end, "viewBox", &vlen);
            if (vb) {
                const char *vp = vb, *ve = vb + vlen;
                vbx = prs_num(&vp, ve);
                vby = prs_num(&vp, ve);
                vbw = prs_num(&vp, ve);
                vbh = prs_num(&vp, ve);
                doc->has_viewbox = 1;
                doc->vbx = vbx; doc->vby = vby;
                doc->vbw = vbw; doc->vbh = vbh;
            }
        } else if (tlen == 4 && tag[0] == 'p' && tag[1] == 'a' && tag[2] == 't' && tag[3] == 'h') {
            int dlen = 0, flen = 0;
            const char *d = find_attr(tag, atts_end, "d", &dlen);
            const char *f = find_attr(tag, atts_end, "fill", &flen);
            if (d) {
                uint32_t col = 0xFFFFFFFF;
                if (f) { const char *fp = f; col = prs_col(&fp, f + flen); }
                svg_poly_t po;
                memset(&po, 0, sizeof(po));
                prs_path(d, dlen, &po);
                if (po.count > 0) {
                    if (doc->count >= doc->cap) {
                        doc->cap = doc->cap ? doc->cap * 2 : 16;
                        svg_shape_t *ns = malloc(doc->cap * sizeof(svg_shape_t));
                        if (!ns) { if (po.pts) free(po.pts); continue; }
                        if (doc->shapes) {
                            for (int i = 0; i < doc->count; i++) {
                                ns[i].fill = doc->shapes[i].fill;
                                ns[i].pts = doc->shapes[i].pts;
                                ns[i].count = doc->shapes[i].count;
                                ns[i].cap = doc->shapes[i].cap;
                            }
                            free(doc->shapes);
                        }
                        doc->shapes = ns;
                    }
                    doc->shapes[doc->count].fill = col;
                    doc->shapes[doc->count].pts = po.pts;
                    doc->shapes[doc->count].count = po.count;
                    doc->shapes[doc->count].cap = po.cap;
                    doc->count++;
                } else {
                    if (po.pts) free(po.pts);
                }
            }
        }
    }

    if (doc->count == 0) { svg_free(doc); return 0; }
    return doc;
}

void svg_render(svg_doc_t *doc, uint32_t *buf, int w, int h, uint32_t bg) {
    if (!doc) return;
    for (int i = 0; i < w * h; i++) buf[i] = bg;
    int sx = doc->vbx, sy = doc->vby, sw = doc->vbw, sh = doc->vbh;
    if (!doc->has_viewbox) { sx = 0; sy = 0; sw = to_fp(100); sh = to_fp(100); }
    for (int i = 0; i < doc->count; i++) {
        svg_poly_t po;
        po.pts = doc->shapes[i].pts;
        po.count = doc->shapes[i].count;
        po.cap = doc->shapes[i].cap;
        fill_poly(&po, buf, w, h, sx, sy, sw, sh, doc->shapes[i].fill);
    }
}

void svg_free(svg_doc_t *doc) {
    if (!doc) return;
    for (int i = 0; i < doc->count; i++)
        if (doc->shapes[i].pts) free(doc->shapes[i].pts);
    if (doc->shapes) free(doc->shapes);
    free(doc);
}
