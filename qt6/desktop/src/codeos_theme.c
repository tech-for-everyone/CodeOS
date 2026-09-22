/* CodeOS Desktop - Theme System Implementation
 * macOS/Windows 11 inspired themes */

#include "codeos_theme.h"
#include <stdint.h>
#include <string.h>

static codeos_theme_t current_theme;

/* Built-in themes */
static const codeos_theme_t theme_dark = {
    .bg_primary    = 0x1E1E2E,
    .bg_secondary  = 0x313244,
    .bg_tertiary   = 0x45475A,
    .accent        = 0x89B4FA,
    .accent_hover  = 0x74C7EC,
    .text_primary  = 0xCDD6F4,
    .text_secondary= 0xA6ADC8,
    .border        = 0x585B70,
    .shadow        = 0x11111B,
    .dock_bg       = 0x1E1E2ECC,
    .menubar_bg    = 0x181825E6,
    .panel_bg      = 0x1E1E2EF0,
    .notification_bg = 0x45475AEE,
    .win_title     = 0xCDD6F4,
    .win_bg        = 0x1E1E2E,
    .win_border    = 0x585B70,
    .win_close     = 0xF38BA8,
    .win_minimize  = 0xF9E2AF,
    .win_maximize  = 0xA6E3A1,
    .success       = 0xA6E3A1,
    .warning       = 0xF9E2AF,
    .error         = 0xF38BA8,
};

static const codeos_theme_t theme_light = {
    .bg_primary    = 0xEFF1F5,
    .bg_secondary  = 0xE6E9EF,
    .bg_tertiary   = 0xCCD0DA,
    .accent        = 0x1E66F5,
    .accent_hover  = 0x209FB5,
    .text_primary  = 0x4C4F69,
    .text_secondary= 0x5C5F77,
    .border        = 0x9CA0B0,
    .shadow        = 0xBCC0CC,
    .dock_bg       = 0xE6E9EFCC,
    .menubar_bg    = 0xEFF1F5E6,
    .panel_bg      = 0xEFF1F5F0,
    .notification_bg = 0xCCD0DAEE,
    .win_title     = 0x4C4F69,
    .win_bg        = 0xEFF1F5,
    .win_border    = 0x9CA0B0,
    .win_close     = 0xD20F39,
    .win_minimize  = 0xDF8E1D,
    .win_maximize  = 0x40A02B,
    .success       = 0x40A02B,
    .warning       = 0xDF8E1D,
    .error         = 0xD20F39,
};

static const codeos_theme_t theme_oled = {
    .bg_primary    = 0x000000,
    .bg_secondary  = 0x0A0A0A,
    .bg_tertiary   = 0x141414,
    .accent        = 0x00D4FF,
    .accent_hover  = 0x00B8E6,
    .text_primary  = 0xFFFFFF,
    .text_secondary= 0xAAAAAA,
    .border        = 0x333333,
    .shadow        = 0x000000,
    .dock_bg       = 0x0A0A0ACC,
    .menubar_bg    = 0x000000E6,
    .panel_bg      = 0x0A0A0AF0,
    .notification_bg = 0x141414EE,
    .win_title     = 0xFFFFFF,
    .win_bg        = 0x0A0A0A,
    .win_border    = 0x333333,
    .win_close     = 0xFF4444,
    .win_minimize  = 0xFFAA00,
    .win_maximize  = 0x44FF44,
    .success       = 0x44FF44,
    .warning       = 0xFFAA00,
    .error         = 0xFF4444,
};

static const codeos_theme_t theme_catppuccin = {
    .bg_primary    = 0x1E1E2E,
    .bg_secondary  = 0x313244,
    .bg_tertiary   = 0x45475A,
    .accent        = 0xF5C2E7,
    .accent_hover  = 0xBA68C8,
    .text_primary  = 0xCDD6F4,
    .text_secondary= 0xA6ADC8,
    .border        = 0x585B70,
    .shadow        = 0x11111B,
    .dock_bg       = 0x1E1E2ECC,
    .menubar_bg    = 0x181825E6,
    .panel_bg      = 0x1E1E2EF0,
    .notification_bg = 0x45475AEE,
    .win_title     = 0xCDD6F4,
    .win_bg        = 0x1E1E2E,
    .win_border    = 0x585B70,
    .win_close     = 0xF38BA8,
    .win_minimize  = 0xF9E2AF,
    .win_maximize  = 0xA6E3A1,
    .success       = 0xA6E3A1,
    .warning       = 0xF9E2AF,
    .error         = 0xF38BA8,
};

static const codeos_theme_t theme_nord = {
    .bg_primary    = 0x2E3440,
    .bg_secondary  = 0x3B4252,
    .bg_tertiary   = 0x434C5E,
    .accent        = 0x88C0D0,
    .accent_hover  = 0x81A1C1,
    .text_primary  = 0xECEFF4,
    .text_secondary= 0xD8DEE9,
    .border        = 0x4C566A,
    .shadow        = 0x242933,
    .dock_bg       = 0x2E3440CC,
    .menubar_bg    = 0x2E3440E6,
    .panel_bg      = 0x2E3440F0,
    .notification_bg = 0x3B4252EE,
    .win_title     = 0xECEFF4,
    .win_bg        = 0x2E3440,
    .win_border    = 0x4C566A,
    .win_close     = 0xBF616A,
    .win_minimize  = 0xEBCB8B,
    .win_maximize  = 0xA3BE8C,
    .success       = 0xA3BE8C,
    .warning       = 0xEBCB8B,
    .error         = 0xBF616A,
};

static const codeos_theme_t theme_dracula = {
    .bg_primary    = 0x282A36,
    .bg_secondary  = 0x44475A,
    .bg_tertiary   = 0x6272A4,
    .accent        = 0xBD93F9,
    .accent_hover  = 0xFF79C6,
    .text_primary  = 0xF8F8F2,
    .text_secondary= 0xBFBFBF,
    .border        = 0x6272A4,
    .shadow        = 0x191A21,
    .dock_bg       = 0x282A36CC,
    .menubar_bg    = 0x282A36E6,
    .panel_bg      = 0x282A36F0,
    .notification_bg = 0x44475AEE,
    .win_title     = 0xF8F8F2,
    .win_bg        = 0x282A36,
    .win_border    = 0x6272A4,
    .win_close     = 0xFF5555,
    .win_minimize  = 0xF1FA8C,
    .win_maximize  = 0x50FA7B,
    .success       = 0x50FA7B,
    .warning       = 0xF1FA8C,
    .error         = 0xFF5555,
};

void codeos_theme_init(void) {
    current_theme = theme_dark;
}

const codeos_theme_t *codeos_theme_current(void) {
    return &current_theme;
}

void codeos_theme_set(const char *theme_name) {
    if (!theme_name) return;
    if (strcmp(theme_name, THEME_DARK) == 0) current_theme = theme_dark;
    else if (strcmp(theme_name, THEME_LIGHT) == 0) current_theme = theme_light;
    else if (strcmp(theme_name, THEME_OLED) == 0) current_theme = theme_oled;
    else if (strcmp(theme_name, THEME_CATPPUCCIN) == 0) current_theme = theme_catppuccin;
    else if (strcmp(theme_name, THEME_NORD) == 0) current_theme = theme_nord;
    else if (strcmp(theme_name, THEME_DRACULA) == 0) current_theme = theme_dracula;
}

void codeos_theme_set_accent(uint32_t color) {
    current_theme.accent = color;
}

const char *codeos_theme_name(void) {
    /* Return name based on current bg */
    if (current_theme.bg_primary == theme_dark.bg_primary) return THEME_DARK;
    if (current_theme.bg_primary == theme_light.bg_primary) return THEME_LIGHT;
    if (current_theme.bg_primary == theme_oled.bg_primary) return THEME_OLED;
    if (current_theme.bg_primary == theme_catppuccin.bg_primary) return THEME_CATPPUCCIN;
    if (current_theme.bg_primary == theme_nord.bg_primary) return THEME_NORD;
    if (current_theme.bg_primary == theme_dracula.bg_primary) return THEME_DRACULA;
    return "Custom";
}

/* Color manipulation */
uint32_t codeos_color_with_alpha(uint32_t color, uint8_t alpha) {
    return (color & 0x00FFFFFF) | ((uint32_t)alpha << 24);
}

uint32_t codeos_color_lighten(uint32_t color, int amount) {
    int r = ((color >> 16) & 0xFF) + amount;
    int g = ((color >> 8) & 0xFF) + amount;
    int b = (color & 0xFF) + amount;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

uint32_t codeos_color_darken(uint32_t color, int amount) {
    int r = ((color >> 16) & 0xFF) - amount;
    int g = ((color >> 8) & 0xFF) - amount;
    int b = (color & 0xFF) - amount;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    return (r << 16) | (g << 8) | b;
}

uint32_t codeos_color_blend(uint32_t fg, uint32_t bg, uint8_t alpha) {
    uint8_t fg_a = (fg >> 24) & 0xFF;
    uint8_t bg_a = (bg >> 24) & 0xFF;
    uint8_t out_a = alpha;

    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_b = fg & 0xFF;

    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;

    uint8_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
    uint8_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
    uint8_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

    return (out_a << 24) | (r << 16) | (g << 8) | b;
}

uint32_t codeos_gradient_sample(const codeos_gradient_t *grad, int x, int y, int w, int h) {
    if (!grad) return 0;
    int t;
    if (grad->vertical) {
        t = (h > 0) ? (y * 255 / h) : 0;
    } else {
        t = (w > 0) ? (x * 255 / w) : 0;
    }

    uint8_t r1 = (grad->color1 >> 16) & 0xFF;
    uint8_t g1 = (grad->color1 >> 8) & 0xFF;
    uint8_t b1 = grad->color1 & 0xFF;

    uint8_t r2 = (grad->color2 >> 16) & 0xFF;
    uint8_t g2 = (grad->color2 >> 8) & 0xFF;
    uint8_t b2 = grad->color2 & 0xFF;

    uint8_t r = r1 + (r2 - r1) * t / 255;
    uint8_t g = g1 + (g2 - g1) * t / 255;
    uint8_t b = b1 + (b2 - b1) * t / 255;

    return (r << 16) | (g << 8) | b;
}
