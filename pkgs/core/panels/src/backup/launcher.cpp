extern "C" {
#include "launcher.h"
#include "panels.h"
#include "wm.h"
#include "string.h"
#include "fb.h"
#include "timer.h"
}

#define LAUNCH_GRID_SZ 64
#define LAUNCH_GAP 12
#define LAUNCH_MAX 16

static int launcher_anim[APP_COUNT_MAX];
static int launcher_target[APP_COUNT_MAX];
static int launcher_open = 0;

void launcher_init(void) {
    launcher_open = 0;
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        launcher_anim[i] = ICON_SZ_LAUNCH;
        launcher_target[i] = ICON_SZ_LAUNCH;
    }
}

void launcher_toggle(void) {
    launcher_open = !launcher_open;
}

int launcher_is_open(void) { return launcher_open; }

void launcher_draw(uint32_t scr_w, uint32_t scr_h) {
    if (!launcher_open) return;

    int grid_n = 0;
    /* Count visible system apps */
    for (int i = 0; i < APP_COUNT_MAX; i++) {
        if (app_is_system[i]) grid_n++;
    }

    int cols = 4;
    int rows = (grid_n + cols - 1) / cols;
    int start_x = (scr_w - (LAUNCH_GRID_SZ * cols + LAUNCH_GAP * (cols - 1))) / 2;
    int start_y = (scr_h - (LAUNCH_GRID_SZ * rows + LAUNCH_GAP * (rows - 1))) / 2 + 30;

    /* Glass background */
    fb_fillrect_gradient_v(start_x - 20, start_y - 20,
                           LAUNCH_GRID_SZ * cols + LAUNCH_GAP * (cols - 1) + 40,
                           LAUNCH_GRID_SZ * rows + LAUNCH_GAP * (rows - 1) + 40,
                           0x201A1A1A, 0x10101010);

    /* Draw icons */
    int idx = 0;
    for (int i = 0; i < APP_COUNT_MAX && idx < LAUNCH_MAX; i++) {
        if (!app_is_system[i]) continue;

        int cx = start_x + (idx % cols) * (LAUNCH_GRID_SZ + LAUNCH_GAP);
        int cy = start_y + (idx / cols) * (LAUNCH_GRID_SZ + LAUNCH_GAP);

        /* Scale animation */
        int sc = launcher_anim[i];
        int sx = cx - (sc - ICON_SZ_LAUNCH) / 2;
        int sy = cy - (sc - ICON_SZ_LAUNCH) / 2;
        int sz = sc;

        /* Glow when hovered/selected */
        /* Simple draw - use icon data */
        if (icon_buf_launch[i]) {
            int icon_x = sx + (sz - ICON_SZ_LAUNCH) / 2;
            int icon_y = sy + (sz - ICON_SZ_LAUNCH) / 2;
            /* Draw icon with scaling */
            for (int yy = 0; yy < ICON_SZ_LAUNCH; yy++) {
                for (int xx = 0; xx < ICON_SZ_LAUNCH; xx++) {
                    uint32_t c = icon_buf_launch[i][yy * ICON_SZ_LAUNCH + xx];
                    if (c) {
                        int dx = icon_x + xx;
                        int dy = icon_y + yy;
                        if (dx >= 0 && dx < scr_w && dy >= 0 && dy < scr_h) {
                            fb_buf[dy * scr_w + dx] = c;
                        }
                    }
                }
            }
        }

        /* Label */
        int label_y = cy + ICON_SZ_LAUNCH + 6;
        fb_drawstr_px(sx, label_y, app_names[i], C_TEXT, 0);

        idx++;
    }
}

int launcher_click(int mx, int my, uint32_t scr_w, uint32_t scr_h) {
    if (!launcher_open) return -1;

    int cols = 4;
    int rows = 4;
    int start_x = (scr_w - (LAUNCH_GRID_SZ * cols + LAUNCH_GAP * (cols - 1))) / 2;
    int start_y = (scr_h - (LAUNCH_GRID_SZ * rows + LAUNCH_GAP * (rows - 1))) / 2 + 30;

    for (int i = 0; i < APP_COUNT_MAX; i++) {
        if (!app_is_system[i]) continue;
        int cx = start_x + (i % cols) * (LAUNCH_GRID_SZ + LAUNCH_GAP) + LAUNCH_GRID_SZ / 2;
        int cy = start_y + (i / cols) * (LAUNCH_GRID_SZ + LAUNCH_GAP) + LAUNCH_GRID_SZ / 2;
        int dx = mx - cx;
        int dy = my - cy;
        if (dx * dx + dy * dy <= (LAUNCH_GRID_SZ / 2) * (LAUNCH_GRID_SZ / 2)) {
            return i;
        }
    }
    return -1;
}

void launcher_update(uint64_t now_ms) {
    /* Animation update - scale icons in/out */
    if (launcher_open) {
        for (int i = 0; i < APP_COUNT_MAX; i++) {
            if (app_is_system[i]) {
                launcher_anim[i] = 1000 + (now_ms % 2000) / 2000 * 400;
            }
        }
    }
}