/* DevStore -- a read-only browser for installed CodeOS applications.
 *
 * The listing is read from the real filesystem with fs_listdir() and
 * fs_get_info(). The previous revision of this file shipped a hardcoded table
 * of entries pointing at paths like /system/bin/terminal, which do not exist
 * on CodeOS -- programs live in /bin. A store that listed names which could
 * never be launched was worse than an empty one, because the names looked
 * correct.
 *
 * Two sources are scanned, both verified to exist in the shipped rootfs:
 *   /bin                      -- installed programs
 *   /usr/share/applications   -- desktop entries (*.desktop)
 *
 * Drawing is restricted to what fb.h actually declares. Earlier revisions
 * called fb_fillrect_gradient_v(), fb_fill_rounded_rect() and
 * fb_fill_rounded_rect_gradient_v() and referenced C_SCROLL, C_WHITE and
 * C_BLUE. None of those symbols is defined anywhere in the tree, which is
 * why this file was never in any build. Rounded corners and vertical
 * gradients would be nicer, but they are unavailable, so flat fills and a
 * one-pixel border are used rather than pretending the helpers exist.
 */

#include "devstore.h"
#include "windows.h"
#include "fs.h"
#include "string.h"
#include "fb.h"
#include "keyboard.h"
#include "pkg.h"
#undef pkg_repo_t

/* Geometry. Fixed, like the other fixed-size panels in this directory; the
 * scr_w/scr_h arguments are accepted for signature compatibility and are
 * deliberately not used to size the window. */
#define DS_W            280
#define DS_H            280
#define DS_ROW_H        20
#define DS_LIST_TOP     56
#define DS_LIST_BOT     250
#define DS_BAR_X        266
#define DS_BAR_W        6
#define DS_BTN_X        8
#define DS_BTN_W        120
#define DS_BTN_H        28
#define DS_BTN_Y        (DS_LIST_BOT + 6)

#define DS_MAX_ENTRIES  128
#define DS_PATH_MAX     256

/* Where an entry came from. Rendered as a glyph so a directory cannot be
 * mistaken for a launchable app. */
enum { DS_APP = 0, DS_DIR = 1, DS_DESKTOP = 2, DS_PKG = 3 };

enum { DS_MODE_INSTALLED = 0, DS_MODE_STORE = 1, DS_MODE_UPDATES = 2 };

struct ds_entry {
    char name[FS_NAME_MAX];
    char path[DS_PATH_MAX];
    char desc[128];
    char ver[32];
    int  type;
    int  installed;
};

static struct ds_entry ds_entries[DS_MAX_ENTRIES];
static int ds_entry_cnt;
static int ds_scroll;
static int ds_mode = DS_MODE_INSTALLED;
/* Open state is tracked separately from the entry count. Folding the two
 * together -- as the previous revision did by returning "ds_entry_cnt > 0" --
 * made devstore_is_open() report closed for a legitimately empty store, and
 * a caller polling that flag would reopen the panel on every tick. */
static int ds_open;

static int ds_rows_visible(void) {
    int rows = (DS_LIST_BOT - DS_LIST_TOP) / DS_ROW_H;
    return rows < 1 ? 1 : rows;
}

/* ds_scroll is clamped here rather than only at scroll time. ds_entry_cnt
 * shrinks when devstore_refresh() re-scans, and a stale offset would render
 * a blank list that reads as "no applications installed". */
static void ds_clamp_scroll(void) {
    int max_off = ds_entry_cnt - ds_rows_visible();
    if (max_off <= 0) {
        ds_scroll = 0;
        return;
    }
    if (ds_scroll > max_off) ds_scroll = max_off;
    if (ds_scroll < 0) ds_scroll = 0;
}

/* ------------------------------------------------------------------ data */

/* Only desktop entries are wanted from /usr/share/applications; anything else
 * there is data, not something the user can launch. fs_get_info() exposes no
 * exec bit, so this suffix test is the only filter available. */
static int ds_has_suffix(const char *s, const char *suffix) {
    size_t sl = strlen(s), fl = strlen(suffix);
    if (fl > sl) return 0;
    return strcmp(s + (sl - fl), suffix) == 0;
}

static void ds_add(const char *name, const char *path, int type) {
    if (ds_entry_cnt >= DS_MAX_ENTRIES) return;
    struct ds_entry *e = &ds_entries[ds_entry_cnt++];
    /* strncpy_safe always NUL-terminates, so no explicit terminator is
     * needed. A name longer than the field is truncated rather than
     * overflowing into `path`. */
    strncpy_safe(e->name, name, sizeof(e->name));
    strncpy_safe(e->path, path, sizeof(e->path));
    e->type = type;
}

/* Join `path` and `leaf` into `out`. Written out by hand because the kernel
 * exposes no path-join helper, and because strcat on a fixed buffer is how
 * panel code overruns stacks. */
static void ds_join(char *out, int out_sz, const char *path, const char *leaf) {
    int at = 0;
    out[0] = 0;
    while (path[at] && at < out_sz - 1) { out[at] = path[at]; at++; }
    out[at] = 0;
    /* A trailing slash on the parent must not double up with the separator
     * added below, or the child name would be prefixed by "//". */
    if (at > 0 && out[at - 1] != '/') {
        if (at < out_sz - 1) { out[at] = '/'; at++; out[at] = 0; }
    }
    int j = 0;
    while (leaf[j] && at < out_sz - 1) { out[at] = leaf[j]; at++; j++; }
    out[at] = 0;
}

/* Recursively collect one directory. Bounded on both axes: `depth` stops a
 * deep tree, and DS_MAX_ENTRIES stops a wide one. Without the depth cap a
 * symlink cycle would walk forever. */
static void ds_scan(const char *path, int depth, int desktop_only) {
    if (depth > 2) return;

    char names[DS_MAX_ENTRIES][FS_NAME_MAX];
    int n = fs_listdir(path, names, DS_MAX_ENTRIES);

    for (int i = 0; i < n; i++) {
        /* "." and ".." are listed by fs_listdir(); descending into them would
         * make the recursion non-terminating. */
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
            continue;
        if (desktop_only && !ds_has_suffix(names[i], ".desktop"))
            continue;

        char child[DS_PATH_MAX];
        ds_join(child, sizeof(child), path, names[i]);

        int size = 0, is_dir = 0;
        if (fs_get_info(child, &size, &is_dir) < 0) continue;

        if (is_dir) {
            if (!desktop_only)
                ds_add(names[i], child, DS_DIR);
            ds_scan(child, depth + 1, desktop_only);
        } else {
            ds_add(names[i], child, desktop_only ? DS_DESKTOP : DS_APP);
        }
    }
}

typedef struct pkg_repo_t {
    const char *name;
    const char *version;
    const char *desc;
    const char *homepage;
    const char *license;
    uint32_t size;
    const char **depends;
    int dep_count;
    uint32_t priority;
    const char **conflicts;
    int conflict_count;
    const char **provides;
    int provides_count;
    int category;
} pkg_repo_t_t;



extern const pkg_repo_t pkg_repo_core[];
extern const pkg_repo_t pkg_repo_extra[];
extern const pkg_repo_t pkg_repo_dev[];
extern const pkg_repo_t pkg_repo_ccp[];
extern const pkg_repo_t pkg_repo_aur[];
extern const pkg_repo_t pkg_repo_android[];
extern pkg_repo_t remote_repo[];
extern const pkg_repo_t *all_repos[];

static int pkg_is_installed(const char *name) {
    extern int pkg_find_installed(const char *name);
    return pkg_find_installed(name) >= 0;
}

static void ds_load_installed(void) {
    ds_entry_cnt = 0;
    ds_scroll = 0;

    /* Installed programs. Every non-directory here is launchable, so no
     * further filtering is applied. */
    ds_scan("/bin", 0, 0);

    /* Desktop entries. Empty in the current rootfs, but it is the correct
     * place for them and scanning it costs one failed lookup. */
    ds_scan("/usr/share/applications", 0, 1);

    ds_clamp_scroll();
}

static void ds_load_store(void) {
    ds_entry_cnt = 0;
    ds_scroll = 0;
    for (int r = 0; all_repos[r]; r++) {
        for (int i = 0; all_repos[r][i].name; i++) {
            const pkg_repo_t *p = &all_repos[r][i];
            if (ds_entry_cnt >= DS_MAX_ENTRIES) break;
            struct ds_entry *e = &ds_entries[ds_entry_cnt++];
            strncpy_safe(e->name, p->name, sizeof(e->name));
            e->path[0] = 0;
            strncpy_safe(e->desc, p->desc, sizeof(e->desc));
            strncpy_safe(e->ver, p->version, sizeof(e->ver));
            e->type = DS_PKG;
            e->installed = pkg_is_installed(p->name) ? 1 : 0;
        }
        if (ds_entry_cnt >= DS_MAX_ENTRIES) break;
    }
    ds_clamp_scroll();
}

void devstore_refresh(void) {
    if (ds_mode == DS_MODE_INSTALLED) {
        ds_load_installed();
    } else if (ds_mode == DS_MODE_STORE) {
        ds_load_store();
    } else {
        ds_entry_cnt = 0;
        ds_scroll = 0;
        ds_clamp_scroll();
    }
}

/* ----------------------------------------------------------------- state */

int devstore_is_open(void) {
    return ds_open;
}

void devstore_open(void) {
    ds_mode = DS_MODE_INSTALLED;
    devstore_refresh();
    ds_open = 1;
}

void devstore_close(void) {
    ds_open = 0;
    ds_entry_cnt = 0;
    ds_scroll = 0;
    ds_mode = DS_MODE_INSTALLED;
}

int devstore_get_entry_count(void) {
    return ds_entry_cnt;
}

int devstore_get_entry_name(int idx, char *buf, int buf_sz) {
    if (idx < 0 || idx >= ds_entry_cnt || buf_sz < 1) return 0;
    strncpy_safe(buf, ds_entries[idx].name, buf_sz);
    return 1;
}

int devstore_get_entry_path(int idx, char *buf, int buf_sz) {
    if (idx < 0 || idx >= ds_entry_cnt || buf_sz < 1) return 0;
    strncpy_safe(buf, ds_entries[idx].path, buf_sz);
    return 1;
}

/* --------------------------------------------------------------- drawing */

void devstore_draw(uint32_t scr_w, uint32_t scr_h) {
    (void)scr_w;
    (void)scr_h;

    ds_clamp_scroll();

    fb_fillrect(0, 0, DS_W, DS_H, C_MANTLE);

    /* A flat 2px rule stands in for the gradient header that the missing
     * fb_fillrect_gradient_v() would have drawn. */
    fb_fillrect(0, 0, DS_W, 2, C_BLUE);
    fb_drawstr_px(10, 14, "DevStore", C_BLUE, C_MANTLE);
    if (ds_mode == DS_MODE_INSTALLED)
        fb_drawstr_px(10, 30, "Installed Apps", C_SUBTEXT1, C_MANTLE);
    else if (ds_mode == DS_MODE_STORE)
        fb_drawstr_px(10, 30, "Available Packages", C_SUBTEXT1, C_MANTLE);
    else
        fb_drawstr_px(10, 30, "CodeOS Application Store", C_SUBTEXT1, C_MANTLE);

    int rows = ds_rows_visible();
    int y = DS_LIST_TOP;
    for (int i = ds_scroll; i < ds_entry_cnt && y < DS_LIST_BOT; i++, y += DS_ROW_H) {
        const char *glyph;
        uint32_t colour;
        switch (ds_entries[i].type) {
        case DS_DIR:     glyph = "/"; colour = C_SKY;     break;
        case DS_DESKTOP: glyph = "="; colour = C_BLUE;    break;
        case DS_PKG:     glyph = "◆"; colour = ds_entries[i].installed ? C_GREEN : C_MAUVE; break;
        default:         glyph = ">"; colour = C_SUBTEXT1; break;
        }
        fb_drawstr_px(10, y, glyph, colour, C_MANTLE);
        fb_drawstr_px(26, y, ds_entries[i].name, C_TEXT, C_MANTLE);
    }

    if (ds_entry_cnt == 0) {
        if (ds_mode == DS_MODE_INSTALLED)
            fb_drawstr_px(10, DS_LIST_TOP, "(nothing installed)", C_SUBTEXT0, C_MANTLE);
        else if (ds_mode == DS_MODE_STORE)
            fb_drawstr_px(10, DS_LIST_TOP, "(no packages found)", C_SUBTEXT0, C_MANTLE);
        else
            fb_drawstr_px(10, DS_LIST_TOP, "(empty)", C_SUBTEXT0, C_MANTLE);
    }

    /* Scrollbar, drawn only when the list actually overflows. Thumb height is
     * proportional with a 12px floor so a long list still gets a visible
     * thumb rather than a sliver. */
    if (ds_entry_cnt > rows) {
        int track = DS_LIST_BOT - DS_LIST_TOP;
        int thumb = (rows * track) / ds_entry_cnt;
        if (thumb < 12) thumb = 12;
        int max_off = ds_entry_cnt - rows;
        int pos = max_off ? (ds_scroll * (track - thumb)) / max_off : 0;
        fb_fillrect(DS_BAR_X, DS_LIST_TOP, DS_BAR_W, track, C_SURFACE1);
        fb_fillrect(DS_BAR_X, DS_LIST_TOP + pos, DS_BAR_W, thumb, C_SUBTEXT0);
    }

    /* Install button. fb_fill_rounded_rect() does not exist, so this is flat
     * with a one-pixel border. */
    fb_fillrect(DS_BTN_X, DS_BTN_Y, DS_BTN_W, DS_BTN_H, C_SURFACE2);
    fb_drawrect(DS_BTN_X, DS_BTN_Y, DS_BTN_W, DS_BTN_H, C_OVERLAY0);
    if (ds_mode == DS_MODE_INSTALLED)
        fb_drawstr_px(DS_BTN_X + 10, DS_BTN_Y + 7, "Browse Store", C_TEXT, C_SURFACE2);
    else if (ds_mode == DS_MODE_STORE) {
        int idx = ds_scroll;
        if (idx >= 0 && idx < ds_entry_cnt && ds_entries[idx].type == DS_PKG && ds_entries[idx].installed)
            fb_drawstr_px(DS_BTN_X + 10, DS_BTN_Y + 7, "Installed", C_TEXT, C_SURFACE2);
        else
            fb_drawstr_px(DS_BTN_X + 10, DS_BTN_Y + 7, "Install Selected", C_TEXT, C_SURFACE2);
    } else
        fb_drawstr_px(DS_BTN_X + 10, DS_BTN_Y + 7, "Install App", C_TEXT, C_SURFACE2);
}

/* ----------------------------------------------------------------- input */

/* Returns the clicked entry index, DS_INSTALL_HIT for the button, or -1.
 * The button is tested first because its rectangle overlaps the list area;
 * testing rows first would swallow clicks aimed at it. */
int devstore_click(int mx, int my) {
    if (mx >= DS_BTN_X && mx < DS_BTN_X + DS_BTN_W &&
        my >= DS_BTN_Y && my < DS_BTN_Y + DS_BTN_H) {
        if (ds_mode == DS_MODE_INSTALLED) {
            ds_mode = DS_MODE_STORE;
            devstore_refresh();
            return DS_MISS;
        }
        /* In store mode, install the selected package if any */
        if (ds_mode == DS_MODE_STORE && ds_entry_cnt > 0 && ds_scroll >= 0) {
            int idx = ds_scroll;
            if (idx < ds_entry_cnt && ds_entries[idx].type == DS_PKG) {
#ifdef KERNEL
                extern void pkg_install_with_deps(const char *name);
                pkg_install_with_deps(ds_entries[idx].name);
#endif
                devstore_refresh();
                return DS_MISS;
            }
        }
        return DS_INSTALL_HIT;
    }

    if (my < DS_LIST_TOP || my >= DS_LIST_BOT) return -1;
    if (mx >= DS_BAR_X) return -1;          /* scrollbar is not clickable */

    int idx = ds_scroll + (my - DS_LIST_TOP) / DS_ROW_H;
    if (idx < 0 || idx >= ds_entry_cnt) return -1;
    return idx;
}

/* Returns 1 if the key was consumed, so the caller knows not to pass it on. */
int devstore_key(int key) {
    int rows = ds_rows_visible();
    int max_off = ds_entry_cnt - rows;

    switch (key) {
    case KEY_UP:
        if (ds_scroll > 0) ds_scroll--;
        ds_clamp_scroll();
        return 1;
    case KEY_DOWN:
        if (max_off > 0 && ds_scroll < max_off) ds_scroll++;
        ds_clamp_scroll();
        return 1;
    case KEY_PGUP:
        ds_scroll -= rows;
        if (ds_scroll < 0) ds_scroll = 0;
        return 1;
    case KEY_PGDN:
        ds_scroll += rows;
        if (ds_scroll > max_off) ds_scroll = max_off;
        if (ds_scroll < 0) ds_scroll = 0;
        return 1;
    case KEY_HOME:
        ds_scroll = 0;
        return 1;
    case KEY_END:
        ds_scroll = max_off > 0 ? max_off : 0;
        return 1;
    case 's':
    case 'S':
    case '\t':
        ds_mode = (ds_mode + 1) % 2;
        devstore_refresh();
        return 1;
    case 'i':
    case 'I':
        ds_mode = DS_MODE_INSTALLED;
        devstore_refresh();
        return 1;
    default:
        return 0;
    }
}

void devstore_mouse_move(int mx, int my) {
    (void)mx;
    (void)my;
}
