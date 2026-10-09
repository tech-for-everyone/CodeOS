#ifndef ANDROID_UI_H
#define ANDROID_UI_H

/* android_ui — tiny Android-style UI toolkit for CodeOS userspace apps.
 *
 * Draws through the kernel window bridge on WM pipe fds 3 (commands) and
 * 4 (events) and falls back to a console transcript when the bridge is not
 * available (e.g. when the app is exec'd outside the desktop).
 *
 * The WM wire format is the one from include/wm_protocol.h; the kernel
 * user-window bridge (kernel/kernel/user_wm.c) implements the other end.
 */

#include <stdint.h>

#include "wm_protocol.h"
#include "unistd.h"
#include "string.h"

/* Android-ish light theme (Material 3 "sky" seed, ARGB) */
#define AU_BG        0xFFFDF6E3
#define AU_SURFACE   0xFFFFFFFF
#define AU_SURFACE2  0xFFEEE9DA
#define AU_PRIMARY   0xFF2563EB
#define AU_BLUE      0xFF2563EB
#define AU_ON_PRIMARY 0xFFFFFFFF
#define AU_TEXT      0xFF1C1917
#define AU_TEXT2     0xFF57534E
#define AU_RED       0xFFEF4444
#define AU_GREEN     0xFF22C55E
#define AU_AMBER     0xFFF59E0B
#define AU_PURPLE    0xFF8B5CF6
#define AU_TEAL      0xFF0D9488
#define AU_BORDER    0xFFD6D3D1

/* Window geometry used by all Android apps (phone-portrait canvas) */
#define AU_W_DEFAULT 320
#define AU_H_DEFAULT 568
#define AU_STATUS_H  24

enum {
    AU_NO_WM  = 0,   /* console mode: no window bridge */
    AU_WM_OK  = 1,   /* window bridge connected */
};

typedef struct {
    int   wm;             /* AU_WM_OK / AU_NO_WM */
    int   win;            /* window id from the bridge */
    int   width, height;  /* canvas size */
    int   closed;         /* set when the window was closed */
    int64_t t0_us;        /* uptime base */
    uint64_t frame;       /* frame counter */
} au_app_t;

/* ── lifecycle ── */
au_app_t *au_start(const char *title, int w, int h);
void      au_end(au_app_t *a);

/* ── drawing (no-ops in console mode) ── */
void au_clear(au_app_t *a);
void au_rect(au_app_t *a, int x, int y, int w, int h, uint32_t col);
void au_outline(au_app_t *a, int x, int y, int w, int h, uint32_t col);
void au_text(au_app_t *a, int x, int y, uint32_t col, const char *s);
void au_flush(au_app_t *a);

/* ── widgets ── */
int  au_button(au_app_t *a, int x, int y, int w, int h,
               const char *label, uint32_t bg, uint32_t fg);
void au_label(au_app_t *a, int x, int y, int w, const char *label,
              uint32_t bg, uint32_t fg);
void au_statusbar(au_app_t *a, const char *title, int hh, int mm);

/* ── event loop helpers ── */
/* au_poll processes one queued event. Returns:
 *   WM_EVENT_FOCUS / WM_EVENT_KEY / WM_EVENT_MOUSE / WM_EVENT_CLOSED / 0(none)
 * Sets a->closed for WM_EVENT_CLOSED. */
int au_poll(au_app_t *a, int *key_out, int *mx, int *my, int *mbuttons);
int au_ms(au_app_t *a);

/* console fallback */
void au_console_line(const char *s); /* used only in console mode */
int  au_console_mode(const au_app_t *a);

/* helpers */
const char *au_version(void);

#endif