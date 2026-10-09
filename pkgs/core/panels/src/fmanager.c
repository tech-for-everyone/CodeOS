#include "fmanager.h"
#include "desktop.h"
#include "windows.h"
#include "fs.h"
#include "string.h"
#include "keyboard.h"
#include "fb.h"
#include "timer.h"

#define FM_WIN_W    680
#define FM_WIN_H    440
#define FM_MAX_ENT  256
#define FM_ROW_H    22
#define FM_SIDEBAR_W 140
#define FM_HEADER_H 24
#define FM_PATH_H   20
#define FM_LIST_Y   (FM_HEADER_H + FM_PATH_H)
#define FM_SCROLLBAR_W 8

static int fm_open;
static int fm_win;
static char fm_path[FS_PATH_MAX];
static char fm_entries[FM_MAX_ENT][FS_NAME_MAX];
static int fm_entry_count;
static int fm_is_dir[FM_MAX_ENT];
static int fm_entry_sizes[FM_MAX_ENT];
static int fm_scroll;
static int fm_selected;
static int fm_has_up;
static int fm_scroll_dragging;
static int fm_scroll_drag_y;
static int fm_scroll_drag_offset;
static int fm_hover_sidebar = -1;
static int fm_hover_file = -1;

#define FM_SIDES 5
static const char *fm_side_names[FM_SIDES] = {"Home", "Root", "etc", "bin", "mnt"};
static const char *fm_side_paths[FM_SIDES] = {"/home", "/", "/etc", "/bin", "/mnt"};
static int fm_side_sel;

static void fm_refresh(void) {
    fm_entry_count = fs_listdir(fm_path, fm_entries, FM_MAX_ENT);
    int plen = strlen(fm_path);
    for (int i = 0; i < fm_entry_count; i++) {
        char full[FS_PATH_MAX];
        int j = 0;
        for (int k = 0; fm_path[k] && j < FS_PATH_MAX - 2; k++) full[j++] = fm_path[k];
        if (plen > 0 && fm_path[plen - 1] != '/') full[j++] = '/';
        for (int k = 0; fm_entries[i][k] && j < FS_PATH_MAX - 1; k++) full[j++] = fm_entries[i][k];
        full[j] = 0;
        int dir = 0, sz = 0;
        if (fs_resolve(full, &dir) >= 0) {
            fm_is_dir[i] = dir;
            fs_get_info(full, &sz, &dir);
            fm_entry_sizes[i] = sz;
        } else {
            fm_is_dir[i] = 0;
            fm_entry_sizes[i] = 0;
        }
    }
    fm_has_up = (plen > 1 || (plen == 1 && fm_path[0] != '/'));
    if (fm_selected >= fm_entry_count) fm_selected = fm_entry_count - 1;
}

int fmanager_open(void) {
    if (fm_open) return 1;
    uint32_t sw = fb_getwidth();
    uint32_t sh = fb_getheight();
    int wx = (sw - FM_WIN_W) / 2;
    int wy = (sh - FM_WIN_H) / 3;
    fm_win = desktop_new_window(wx, wy, FM_WIN_W, FM_WIN_H, "Explorer", 0xFFFFFFFF, C_MANTLE);
    if (fm_win < 0) return 0;
    fm_open = 1;
    fm_scroll = 0;
    fm_selected = -1;
    fm_side_sel = 0;
    fm_scroll_dragging = 0;
    strlcpy(fm_path, "/", sizeof(fm_path));
    fm_refresh();
    desktop_redraw();
    return 1;
}

int fmanager_is_open(void) { return fm_open; }
void fmanager_close(void) { fm_open = 0; fm_scroll_dragging = 0; }

static void fm_navigate(const char *path) {
    int dir;
    if (fs_resolve(path, &dir) >= 0 && dir) {
        strncpy_safe(fm_path, path, FS_PATH_MAX);
        fm_scroll = 0;
        fm_selected = -1;
        fm_refresh();
        desktop_redraw();
    }
}

static void fm_open_selected(void) {
    if (fm_selected < 0 || fm_selected >= fm_entry_count) return;
    if (fm_is_dir[fm_selected]) {
        char full[FS_PATH_MAX];
        int plen = strlen(fm_path);
        int j = 0;
        for (int k = 0; fm_path[k] && j < FS_PATH_MAX - 2; k++) full[j++] = fm_path[k];
        if (plen > 0 && fm_path[plen - 1] != '/') full[j++] = '/';
        for (int k = 0; fm_entries[fm_selected][k] && j < FS_PATH_MAX - 1; k++)
            full[j++] = fm_entries[fm_selected][k];
        full[j] = 0;
        fm_navigate(full);
    }
}

static void fm_go_parent(void) {
    if (!fm_has_up) return;
    char parent[FS_PATH_MAX];
    int plen = strlen(fm_path);
    if (plen <= 1) strlcpy(parent, "/", sizeof(parent));
    else {
        int last = plen - 1;
        if (fm_path[last] == '/') last--;
        while (last > 0 && fm_path[last] != '/') last--;
        if (last == 0) { parent[0] = '/'; parent[1] = 0; }
        else { memcpy(parent, fm_path, last); parent[last] = 0; }
    }
    fm_navigate(parent);
}

static void fm_create_folder(void) {
    char full[FS_PATH_MAX];
    int plen = strlen(fm_path);
    int j = 0;
    for (int k = 0; fm_path[k] && j < FS_PATH_MAX - 2; k++) full[j++] = fm_path[k];
    if (plen > 0 && fm_path[plen - 1] != '/') full[j++] = '/';
    const char *name = "New Folder";
    for (int k = 0; name[k] && j < FS_PATH_MAX - 1; k++) full[j++] = name[k];
    full[j] = 0;
    fs_mkdir(full);
    fm_refresh();
    desktop_redraw();
}

static void fm_delete_selected(void) {
    if (fm_selected < 0 || fm_selected >= fm_entry_count) return;
    char full[FS_PATH_MAX];
    int plen = strlen(fm_path);
    int j = 0;
    for (int k = 0; fm_path[k] && j < FS_PATH_MAX - 2; k++) full[j++] = fm_path[k];
    if (plen > 0 && fm_path[plen - 1] != '/') full[j++] = '/';
    for (int k = 0; fm_entries[fm_selected][k] && j < FS_PATH_MAX - 1; k++)
        full[j++] = fm_entries[fm_selected][k];
    full[j] = 0;
    if (fm_is_dir[fm_selected]) fs_rmdir(full); else fs_rm(full);
    fm_refresh();
    desktop_redraw();
}

static void fm_format_size(int bytes, char *buf, int bufsz) {
    if (bytes < 1024) {
        int n = 0, v = bytes; char tmp[8];
        if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
        while (v > 0) { tmp[n++] = '0' + v % 10; v /= 10; }
        int i = 0;
        while (n > 0 && i < bufsz - 1) buf[i++] = tmp[--n];
        buf[i] = 0;
    } else if (bytes < 1024 * 1024) {
        int kb = bytes / 1024;
        int rem = (bytes % 1024) * 10 / 1024;
        int n = 0, v = kb; char tmp[8];
        while (v > 0) { tmp[n++] = '0' + v % 10; v /= 10; }
        int i = 0;
        while (n > 0 && i < bufsz - 4) buf[i++] = tmp[--n];
        buf[i++] = '.';
        buf[i++] = '0' + rem;
        buf[i++] = 'K';
        buf[i] = 0;
    } else {
        int mb = bytes / (1024 * 1024);
        int rem = (bytes % (1024 * 1024)) * 10 / (1024 * 1024);
        int n = 0, v = mb; char tmp[8];
        while (v > 0) { tmp[n++] = '0' + v % 10; v /= 10; }
        int i = 0;
        while (n > 0 && i < bufsz - 4) buf[i++] = tmp[--n];
        buf[i++] = '.';
        buf[i++] = '0' + rem;
        buf[i++] = 'M';
        buf[i] = 0;
    }
}

static int fm_get_max_vis(window_t *w) {
    int ch = w->h - 26;
    return (ch - FM_LIST_Y - 4) / FM_ROW_H;
}

static int fm_get_list_h(window_t *w) {
    int ch = w->h - 26;
    return ch - FM_LIST_Y - 4;
}

void fmanager_draw(void) {
    if (!fm_open) return;
    window_t *w = desktop_get_window(fm_win);
    if (!w || !w->visible) return;

    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;
    if (cw < 20 || ch < 20) return;

    fb_fillrect(cx, cy, cw, ch, C_MANTLE);

    fb_fillrect(cx, cy, FM_SIDEBAR_W, ch, C_CRUST);
    fb_fillrect(cx + FM_SIDEBAR_W, cy, 1, ch, C_SURFACE1);

    { /* Sidebar */
        int sy = cy + 6;
        for (int i = 0; i < FM_SIDES; i++) {
            int item_h = 22;
            uint32_t sbg = (i == fm_side_sel) ? C_SURFACE0 : C_CRUST;
            if (i == fm_hover_sidebar && i != fm_side_sel) sbg = C_GLASS_HI;
            fb_fillrect(cx + 4, sy, FM_SIDEBAR_W - 8, item_h, sbg);
            fb_fill_rounded_rect(cx + 6, sy + 3, 16, 16, 4, C_SKY);
            char ic[2] = {fm_side_names[i][0], 0};
            fb_drawstr_px(cx + 10, sy + 3, ic, C_CRUST, C_SKY);
            fb_drawstr_px(cx + 26, sy + 3, fm_side_names[i], C_TEXT, sbg);
            sy += item_h + 2;
        }
    }

    int lx = cx + FM_SIDEBAR_W + 1;
    int lw = cw - FM_SIDEBAR_W - 1 - FM_SCROLLBAR_W;

    fb_fillrect(lx, cy, lw, FM_HEADER_H, C_MANTLE);
    fb_drawstr_px(lx + 8, cy + (FM_HEADER_H - 16) / 2, "Files", C_TEXT, C_MANTLE);
    fb_fillrect(lx, cy + FM_HEADER_H - 1, lw, 1, C_SURFACE1);

    int py = cy + FM_HEADER_H;
    fb_fillrect(lx, py, lw, FM_PATH_H, C_SURFACE0);
    int bread_cx = lx + 6;
    char path_copy[FS_PATH_MAX];
    strlcpy(path_copy, fm_path, sizeof(path_copy));
    char *part = path_copy;
    int first = 1;
    while (*part) {
        char *slash = part;
        while (*slash && *slash != '/') slash++;
        int was_slash = (*slash == '/');
        if (slash != part || first) {
            *slash = 0;
            if (!first) {
                fb_drawstr_px(bread_cx, py + (FM_PATH_H - 16) / 2, "/", C_OVERLAY0, C_SURFACE0);
                bread_cx += 8;
            }
            int pl = strlen(part);
            if (pl > 0) fb_drawstr_px(bread_cx, py + (FM_PATH_H - 16) / 2, part, first ? C_SKY : C_TEXT, C_SURFACE0);
            bread_cx += pl * 8;
            first = 0;
        }
        if (was_slash) { part = slash + 1; } else break;
    }

    int ly = py + FM_PATH_H;
    fb_fillrect(lx, ly, lw, 18, C_BASE);
    fb_drawstr_px(lx + 36, ly + 1, "Name", C_SUBTEXT0, C_BASE);
    fb_drawstr_px(lx + lw - 70, ly + 1, "Size", C_SUBTEXT0, C_BASE);
    fb_fillrect(lx, ly + 17, lw, 1, C_SURFACE1);

    int max_vis = fm_get_max_vis(w);
    if (max_vis < 1) return;

    for (int i = fm_scroll; i < fm_entry_count && i < fm_scroll + max_vis; i++) {
        int row = i - fm_scroll;
        int ry = ly + 18 + row * FM_ROW_H;
        uint32_t bg = (i == fm_selected) ? C_SURFACE1 : C_MANTLE;
        if (i == fm_hover_file && i != fm_selected) bg = C_GLASS_HI;
        fb_fillrect(lx, ry, lw, FM_ROW_H, bg);

        if (fm_is_dir[i]) {
            fb_fill_rounded_rect(lx + 6, ry + 3, 16, 16, 4, C_SKY);
            fb_drawstr_px(lx + 10, ry + 3, "D", C_CRUST, C_SKY);
        } else {
            fb_fill_rounded_rect(lx + 6, ry + 3, 16, 16, 4, C_SURFACE2);
            fb_drawstr_px(lx + 10, ry + 3, "F", C_CRUST, C_SURFACE2);
        }

        char line[FS_NAME_MAX + 2];
        int k;
        for (k = 0; fm_entries[i][k] && k < FS_NAME_MAX; k++) line[k] = fm_entries[i][k];
        line[k] = 0;

        uint32_t name_col = fm_is_dir[i] ? C_SKY : C_TEXT;
        fb_drawstr_px(lx + 28, ry + 3, line, name_col, bg);

        char szbuf[16];
        if (fm_is_dir[i]) {
            strlcpy(szbuf, "--", sizeof(szbuf));
        } else {
            fm_format_size(fm_entry_sizes[i], szbuf, sizeof(szbuf));
        }
        fb_drawstr_px(lx + lw - 70, ry + 3, szbuf, C_OVERLAY0, bg);
    }

    /* Scrollbar */
    int sb_x = lx + lw;
    int sb_h = fm_get_list_h(w);
    int sb_y = ly + 18;
    fb_fillrect(sb_x, sb_y, FM_SCROLLBAR_W, sb_h, C_BASE);
    if (fm_entry_count > max_vis) {
        int thumb_h = (sb_h * max_vis) / fm_entry_count;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = sb_y;
        if (fm_entry_count > max_vis) {
            thumb_y = sb_y + ((sb_h - thumb_h) * fm_scroll) / (fm_entry_count - max_vis);
        }
        fb_fill_rounded_rect(sb_x + 1, thumb_y, FM_SCROLLBAR_W - 2, thumb_h, 3, C_SURFACE1);
    }
}

int fmanager_click(int mx, int my) {
    if (!fm_open) return 0;
    window_t *w = desktop_get_window(fm_win);
    if (!w || !w->visible) return 0;
    desktop_set_focused(fm_win);
    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;
    if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch) return 0;

    if (mx < cx + FM_SIDEBAR_W) {
        int sy = cy + 6;
        for (int i = 0; i < FM_SIDES; i++) {
            if (my >= sy && my < sy + 22) {
                fm_side_sel = i;
                fm_navigate(fm_side_paths[i]);
                return 1;
            }
            sy += 24;
        }
        return 1;
    }

    int py = cy + FM_HEADER_H;

    if (my >= py && my < py + FM_PATH_H) {
        fm_go_parent();
        return 1;
    }

    int ly = py + FM_PATH_H;
    int list_start = ly + 18;

    /* Scrollbar click */
    int lx = cx + FM_SIDEBAR_W + 1;
    int lw = cw - FM_SIDEBAR_W - 1 - FM_SCROLLBAR_W;
    int sb_x = lx + lw;
    int sb_h = fm_get_list_h(w);
    int sb_y = ly + 18;
    int max_vis = fm_get_max_vis(w);

    if (mx >= sb_x && mx < sb_x + FM_SCROLLBAR_W && my >= sb_y && my < sb_y + sb_h) {
        if (fm_entry_count > max_vis) {
            int thumb_h = (sb_h * max_vis) / fm_entry_count;
            if (thumb_h < 20) thumb_h = 20;
            fm_scroll_dragging = 1;
            fm_scroll_drag_y = my;
            fm_scroll_drag_offset = my - (sb_y + ((sb_h - thumb_h) * fm_scroll) / (fm_entry_count - max_vis));
        }
        return 1;
    }

    /* Scrollbar track click - jump to position */
    if (mx >= sb_x && mx < sb_x + FM_SCROLLBAR_W && my >= sb_y && my < sb_y + sb_h) {
        if (fm_entry_count > max_vis) {
            int thumb_h = (sb_h * max_vis) / fm_entry_count;
            if (thumb_h < 20) thumb_h = 20;
            int scroll_range = sb_h - thumb_h;
            if (scroll_range > 0) {
                int target = ((my - sb_y) * (fm_entry_count - max_vis)) / scroll_range;
                if (target < 0) target = 0;
                if (target > fm_entry_count - max_vis) target = fm_entry_count - max_vis;
                fm_scroll = target;
            }
        }
        return 1;
    }

    if (my >= list_start) {
        int row = (my - list_start) / FM_ROW_H;
        if (row >= 0 && row < max_vis) {
            int idx = fm_scroll + row;
            if (idx < fm_entry_count) {
                if (idx == fm_selected && fm_is_dir[idx])
                    fm_open_selected();
                else
                    fm_selected = idx;
                desktop_redraw();
            }
            return 1;
        }
    }

    return 0;
}

int fmanager_mousemove(int mx, int my) {
    if (!fm_open) return 0;
    window_t *w = desktop_get_window(fm_win);
    if (!w || !w->visible) return 0;
    int cx = w->x + 4;
    int cy = w->y + 22;
    int cw = w->w - 8;
    int ch = w->h - 26;

    int old_hover_sidebar = fm_hover_sidebar;
    int old_hover_file = fm_hover_file;
    fm_hover_sidebar = -1;
    fm_hover_file = -1;

    if (mx >= cx && mx < cx + FM_SIDEBAR_W && my >= cy && my < cy + ch) {
        int sy = cy + 6;
        for (int i = 0; i < FM_SIDES; i++) {
            if (my >= sy && my < sy + 22) {
                fm_hover_sidebar = i;
                break;
            }
            sy += 24;
        }
    }

    int lx = cx + FM_SIDEBAR_W + 1;
    int lw = cw - FM_SIDEBAR_W - 1 - FM_SCROLLBAR_W;
    int py = cy + FM_HEADER_H;
    int ly = py + FM_PATH_H;
    int list_start = ly + 18;
    int max_vis = fm_get_max_vis(w);

    if (mx >= lx && mx < lx + lw && my >= list_start) {
        int row = (my - list_start) / FM_ROW_H;
        if (row >= 0 && row < max_vis) {
            int idx = fm_scroll + row;
            if (idx < fm_entry_count)
                fm_hover_file = idx;
        }
    }

    if (fm_scroll_dragging) {
        int sb_h = fm_get_list_h(w);
        int sb_y = ly + 18;

        if (fm_entry_count > max_vis) {
            int thumb_h = (sb_h * max_vis) / fm_entry_count;
            if (thumb_h < 20) thumb_h = 20;
            int scroll_range = sb_h - thumb_h;
            int new_thumb_y = my - fm_scroll_drag_offset - sb_y;
            if (new_thumb_y < 0) new_thumb_y = 0;
            if (new_thumb_y > scroll_range) new_thumb_y = scroll_range;
            fm_scroll = (new_thumb_y * (fm_entry_count - max_vis)) / scroll_range;
        }
    }

    if (fm_hover_sidebar != old_hover_sidebar || fm_hover_file != old_hover_file || fm_scroll_dragging) {
        desktop_redraw();
        return 1;
    }
    return 0;
}

int fmanager_mouseup(void) {
    if (fm_scroll_dragging) {
        fm_scroll_dragging = 0;
        return 1;
    }
    return 0;
}

void fmanager_key(int key) {
    if (!fm_open) return;
    window_t *w = desktop_get_window(fm_win);
    if (!w || !w->visible) return;
    if (desktop_focused_window() != fm_win) return;

    int changed = 0;
    switch (key) {
        case 'w': case 'W':
            fmanager_close(); desktop_redraw(); return;
        case KEY_UP: case 'k': case 'K':
            if (fm_selected > 0) { fm_selected--; changed = 1; } break;
        case KEY_DOWN: case 'j': case 'J':
            if (fm_selected < fm_entry_count - 1) { fm_selected++; changed = 1; } break;
        case '\n': case '\r':
            fm_open_selected(); return;
        case '\b':
            fm_go_parent(); return;
        case KEY_DEL:
            fm_delete_selected(); return;
        case 'n': case 'N':
            fm_create_folder(); return;
    }

    if (changed) {
        window_t *w2 = desktop_get_window(fm_win);
        if (w2) {
            int max_vis = fm_get_max_vis(w2);
            if (fm_selected < fm_scroll) fm_scroll = fm_selected;
            if (fm_selected >= fm_scroll + max_vis) fm_scroll = fm_selected - max_vis + 1;
        }
        desktop_redraw();
    }
}
