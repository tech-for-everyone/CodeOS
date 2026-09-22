/* CodeOS Desktop - Theme System
 * Supports light/dark themes and accent colors */

#ifndef CODEOS_THEME_H
#define CODEOS_THEME_H

#include <stdint.h>

/* Theme colors */
typedef struct {
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_tertiary;
    uint32_t accent;
    uint32_t accent_hover;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t border;
    uint32_t shadow;
    uint32_t dock_bg;
    uint32_t menubar_bg;
    uint32_t panel_bg;
    uint32_t notification_bg;
    uint32_t win_title;
    uint32_t win_bg;
    uint32_t win_border;
    uint32_t win_close;
    uint32_t win_minimize;
    uint32_t win_maximize;
    uint32_t success;
    uint32_t warning;
    uint32_t error;
} codeos_theme_t;

/* Built-in themes */
#define THEME_DARK     "CodeOS Dark"
#define THEME_LIGHT    "CodeOS Light"
#define THEME_OLED     "CodeOS OLED"
#define THEME_CATPPUCCIN "Catppuccin Mocha"
#define THEME_NORD     "Nord"
#define THEME_DRACULA  "Dracula"

/* Theme API */
void codeos_theme_init(void);
const codeos_theme_t *codeos_theme_current(void);
void codeos_theme_set(const char *theme_name);
void codeos_theme_set_accent(uint32_t color);
const char *codeos_theme_name(void);

/* Color manipulation */
uint32_t codeos_color_with_alpha(uint32_t color, uint8_t alpha);
uint32_t codeos_color_lighten(uint32_t color, int amount);
uint32_t codeos_color_darken(uint32_t color, int amount);
uint32_t codeos_color_blend(uint32_t fg, uint32_t bg, uint8_t alpha);

/* Gradient definitions */
typedef struct {
    uint32_t color1;
    uint32_t color2;
    int vertical; /* 1=vertical, 0=horizontal */
} codeos_gradient_t;

uint32_t codeos_gradient_sample(const codeos_gradient_t *grad, int x, int y, int w, int h);

#endif /* CODEOS_THEME_H */
