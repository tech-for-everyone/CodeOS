extern "C" {
#include "devstore.h"
#include "wm.h"
#include "string.h"
#include "fb.h"
}

#define DEVSTORE_MAX_ENTRIES 64

static struct {
    char name[64];
    char path[128];
    int type; /* 0=app, 1=data, 2=system */
} devstore_entries[DEVSTORE_MAX_ENTRIES];
static int ds_entry_cnt = 0;

void devstore_init(void) {
    ds_entry_cnt = 0;
    /* Pre-populate with some system entries */
    if (ds_entry_cnt < DEVSTORE_MAX_ENTRIES) {
        strncpy(devstore_entries[ds_entry_cnt].name, "Terminal", 63);
        strncpy(devstore_entries[ds_entry_cnt].path, "/system/bin/terminal", 127);
        devstore_entries[ds_entry_cnt].type = 0;
        ds_entry_cnt++;
    }
    if (ds_entry_cnt < DEVSTORE_MAX_ENTRIES) {
        strncpy(devstore_entries[ds_entry_cnt].name, "Calculator", 63);
        strncpy(devstore_entries[ds_entry_cnt].path, "/system/bin/calc", 127);
        devstore_entries[ds_entry_cnt].type = 0;
        ds_entry_cnt++;
    }
    if (ds_entry_cnt < DEVSTORE_MAX_ENTRIES) {
        strncpy(devstore_entries[ds_entry_cnt].name, "Files", 63);
        strncpy(devstore_entries[ds_entry_cnt].path, "/system/bin/fmanager", 127);
        devstore_entries[ds_entry_cnt].type = 0;
        ds_entry_cnt++;
    }
    if (ds_entry_cnt < DEVSTORE_MAX_ENTRIES) {
        strncpy(devstore_entries[ds_entry_cnt].name, "Settings", 63);
        strncpy(devstore_entries[ds_entry_cnt].path, "/system/bin/settings", 127);
        devstore_entries[ds_entry_cnt].type = 0;
        ds_entry_cnt++;
    }
}

int devstore_get_entry_count(void) { return ds_entry_cnt; }

int devstore_get_entry_name(int idx, char *buf, int buf_sz) {
    if (idx < 0 || idx >= ds_entry_cnt) return 0;
    strncpy(buf, devstore_entries[idx].name, buf_sz - 1);
    buf[buf_sz - 1] = 0;
    return 1;
}

int devstore_get_entry_path(int idx, char *buf, int buf_sz) {
    if (idx < 0 || idx >= ds_entry_cnt) return 0;
    strncpy(buf, devstore_entries[idx].path, buf_sz - 1);
    buf[buf_sz - 1] = 0;
    return 1;
}

void devstore_draw(uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w; (void)scr_h;

    fb_fillrect_gradient_v(0, 0, 280, 240,
                           0x30252525, 0x20151515);

    fb_drawstr_px(8, 20, "DevStore", C_BLUE, 0);

    int y = 50;
    for (int i = 0; i < ds_entry_cnt && y < 220; i++) {
        fb_drawstr_px(10, y, devstore_entries[i].name, C_TEXT, 0);
        y += 24;
    }

    /* Install button */
    int by = ds_entry_cnt < DEVSTORE_MAX_ENTRIES ? 220 : 220;
    fb_fill_rounded_rect(8, by, 120, 24, 6, 0x400066FF);
    fb_drawstr_px(10, by + 3, "Install App", C_WHITE, 0);
}

int devstore_click(int mx, int my) {
    (void)scr_w; (void)scr_h;

    int y = 50;
    for (int i = 0; i < ds_entry_cnt; i++) {
        if (mx >= 10 && mx < 270 && my >= y && my < y + 24) {
            return i;
        }
        y += 24;
    }

    /* Install */
    if (mx >= 8 && mx < 128 && my >= 220 && my < 244) {
        return ds_entry_cnt; /* Install flag */
    }

    return -1;
}

int devstore_key(int key) {
    (void)key;
    return 0;
}

void devstore_mouse_move(int mx, int my) {
    (void)mx; (void)my;
}