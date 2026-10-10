#ifndef CURSOR_THEME_H
#define CURSOR_THEME_H

#include <stdint.h>

/* Xcursor (.cursor) theme loader.
 *
 * The kernel's built-in mouse cursor is an 8-entry table of 16x16 1-bit
 * bitmaps (see fb.cpp). That is enough to be visible but it is not a theme: it
 * cannot be anti-aliased and it cannot be changed without editing the kernel.
 * This module reads real Xcursor theme files off the mounted filesystem and
 * hands fb.cpp a 32-bit ARGB surface per style, so the cursor that ships is
 * the Bibata Modern Classic assets rather than a hand-drawn approximation.
 *
 * Format (Xcursor, all little-endian):
 *
 *   header  { magic "Xcur", header_size=16, version, ntoc }
 *   toc[ntoc] { type, subtype, position }        // subtype == nominal size
 *   chunk at position:
 *       { header=36, type, subtype, version,
 *         width, height, xhot, yhot, delay,
 *         width*height * uint32 0xAARRGGBB }
 *
 * Only image chunks (type 0xfffd0002) are used. Animated cursors carry many
 * chunks per size; the first one at the chosen size is taken, which is what a
 * static pointer wants.
 */

#define CURSOR_THEME_MAX_DIM 64
#define CURSOR_THEME_STYLES  8

typedef struct {
    int       width;
    int       height;
    int       hotx;   /* hotspot, pixels from the left edge */
    int       hoty;   /* hotspot, pixels from the top edge  */
    uint32_t *argb;   /* width*height, 0xAARRGGBB (alpha in the high byte) */
} cursor_theme_image_t;

/* Load the cursor set from `dir` (a theme's "cursors" directory). Every style
 * that resolves is loaded; a style whose file is missing or malformed keeps its
 * built-in fallback. Returns the number of styles loaded (0..CURSOR_THEME_STYLES).
 * Safe to call when no filesystem is mounted -- it simply loads nothing. */
int cursor_theme_load(const char *dir);

void cursor_theme_unload(void);

/* NULL when `style` was not loaded (fall back to the built-in bitmap). */
const cursor_theme_image_t *cursor_theme_get(int style);

int cursor_theme_loaded_count(void);
int cursor_theme_any_loaded(void);

/* Which size in a file to prefer; the nearest available is used. */
void cursor_theme_set_target(int px);
int  cursor_theme_get_target(void);

const char *cursor_theme_style_name(int style);
const char *cursor_theme_style_file(int style);

#endif /* CURSOR_THEME_H */
