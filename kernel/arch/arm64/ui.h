#ifndef ZIRCON_ARM64_UI_H
#define ZIRCON_ARM64_UI_H

#include "input.h"
#include "types.h"

/* A self-contained "app" in the ARM64 phone UI.
 *
 * The UI framework owns the status bar, home screen, navigation and event
 * dispatch.  An app only draws its own content area and, if it wants, reacts
 * to events.  Everything is static: there is no allocator in this kernel. */
typedef struct ui_app ui_app_t;

struct ui_app {
    const char *name;
    uint32_t color;   /* icon tile colour */
    int icon;         /* display.h ICON_* id */

    void (*enter)(ui_app_t *self);
    void (*draw)(ui_app_t *self, int x, int y, int w, int h);
    void (*event)(ui_app_t *self, const input_event_t *e);
    void (*tick)(ui_app_t *self);
};

/* Register an app for the home grid (max 8, ignored past that). */
void ui_register(ui_app_t *app);

/* Build the registry, render the home screen and loop forever. */
void ui_init(void);
void ui_run(void) __attribute__((noreturn));

/* Used by apps to navigate. */
void ui_launch(ui_app_t *app);
void ui_home(void);

#endif