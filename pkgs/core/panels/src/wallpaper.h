#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <stdint.h>

#define WALLPAPER_MAX_WORKSPACES 8

typedef enum {
    WALLPAPER_SOLID,
    WALLPAPER_GRADIENT_V,
    WALLPAPER_GRADIENT_H,
    WALLPAPER_GRADIENT_V3,
    WALLPAPER_GRADIENT_DIAG,
    WALLPAPER_SVG,
    WALLPAPER_CHROMEOS,
    WALLPAPER_SCENE,      /* multi-layer Big Sur style (orbs + vignette) */
} wallpaper_type_t;

typedef struct {
    wallpaper_type_t type;
    uint32_t color1;       /* top/left color */
    uint32_t color2;       /* bottom/right color */
    const char *svg_src;   /* SVG XML source (if type == SVG) or extra color ptr */
    int svg_len;
} wallpaper_t;

void wallpaper_init(void);
void wallpaper_set(int workspace, wallpaper_t *wp);
wallpaper_t *wallpaper_get(int workspace);
void wallpaper_invalidate(void); /* force rebuild of cached bitmap */

void wallpaper_draw(int workspace, uint32_t *buf, int stride, int x, int y, int w, int h);

/* Built-in presets */
extern wallpaper_t wallpaper_default;
extern wallpaper_t wallpaper_dark_gradient;
extern wallpaper_t wallpaper_light;
extern wallpaper_t wallpaper_mountains;
extern wallpaper_t wallpaper_thormium;
extern wallpaper_t wallpaper_macos_big_sur;
extern wallpaper_t wallpaper_macos27;
extern wallpaper_t wallpaper_vibrant;
extern wallpaper_t wallpaper_aurora;
extern wallpaper_t wallpaper_tahoe;

#endif
