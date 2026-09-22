#ifndef CODEOS_LITEHTML_H
#define CODEOS_LITEHTML_H

#include <stdint.h>
#include <litehtml.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct codeos_litehtml_context codeos_litehtml_context_t;

typedef void (*codeos_draw_callback_t)(void *user_data, int x, int y, int w, int h, uint32_t color);
typedef void *(*codeos_font_load_t)(void *user_data, const char *family, int size, int weight, int style);
typedef void (*codeos_font_free_t)(void *user_data, void *font);
typedef void (*codeos_text_draw_t)(void *user_data, void *font, int x, int y, const char *text, int len, uint32_t color);
typedef void (*codeos_image_load_t)(void *user_data, const char *src, int *w, int *h);
typedef void (*codeos_image_draw_t)(void *user_data, void *img, int x, int y, int w, int h);

typedef struct {
    codeos_draw_callback_t draw_rect;
    codeos_font_load_t font_load;
    codeos_font_free_t font_free;
    codeos_text_draw_t text_draw;
    codeos_image_load_t image_load;
    codeos_image_draw_t image_draw;
    int (*script_exec)(void *user_data, const char *js_source, char *output, int max_out);
    void *user_data;
} codeos_render_callbacks_t;

codeos_litehtml_context_t *codeos_litehtml_create(const codeos_render_callbacks_t *callbacks);
void codeos_litehtml_destroy(codeos_litehtml_context_t *ctx);

int codeos_litehtml_load_html(codeos_litehtml_context_t *ctx, const char *html, int len, int width);
void codeos_litehtml_render(codeos_litehtml_context_t *ctx, int x, int y);
int codeos_litehtml_get_height(codeos_litehtml_context_t *ctx);
int codeos_litehtml_get_width(codeos_litehtml_context_t *ctx);
void codeos_litehtml_scroll(codeos_litehtml_context_t *ctx, int y);

int codeos_litehtml_on_click(codeos_litehtml_context_t *ctx, int x, int y);
int codeos_litehtml_on_scroll(codeos_litehtml_context_t *ctx, int dy);

#ifdef __cplusplus
}
#endif

#endif