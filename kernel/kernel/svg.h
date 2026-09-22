#ifndef SVG_H
#define SVG_H

#include "types.h"

typedef struct { int x, y; } svg_pt_t;

typedef struct {
    svg_pt_t *pts;
    int count, cap;
} svg_poly_t;

typedef struct {
    uint32_t fill;
    svg_pt_t *pts;
    int count, cap;
} svg_shape_t;

typedef struct {
    float vbx, vby, vbw, vbh;
    svg_shape_t *shapes;
    int count, cap;
    int has_viewbox;
} svg_doc_t;

svg_doc_t *svg_parse(const char *xml, int len);
void svg_render(svg_doc_t *doc, uint32_t *buf, int w, int h, uint32_t bg);
void svg_free(svg_doc_t *doc);

#endif
