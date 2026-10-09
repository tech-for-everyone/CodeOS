#ifndef KB_LAYOUT_H
#define KB_LAYOUT_H

#include <stdint.h>

typedef enum {
    KB_LAYOUT_QWERTY = 0,
    KB_LAYOUT_AZERTY,
    KB_LAYOUT_QWERTZ,
    KB_LAYOUT_DVORAK,
    KB_LAYOUT_COLEMAK,
    KB_LAYOUT_COUNT
} kb_layout_id_t;

typedef struct {
    const char    *name;
    const uint8_t *normal;   /* 128-entry scancode→ASCII (unshifted) */
    const uint8_t *shifted;  /* 128-entry scancode→ASCII (shifted) */
} kb_layout_t;

/* Set the active keyboard layout. Returns 0 on success. */
int  kb_layout_set(kb_layout_id_t id);

/* Get the currently active layout tables */
const kb_layout_t *kb_layout_active(void);

/* Get the currently active layout id */
kb_layout_id_t kb_layout_current(void);

/* Get layout by id */
const kb_layout_t *kb_layout_get(kb_layout_id_t id);

/* Lookup layout id by name string (case-insensitive prefix match).
 * Returns -1 if no match. */
int  kb_layout_find(const char *name);

/* Get a printable name for the current layout */
const char *kb_layout_name(void);

#endif
