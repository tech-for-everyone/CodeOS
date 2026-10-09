#ifndef SDL3_DESKTOP_H
#define SDL3_DESKTOP_H

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdbool.h>

#include "sdl3_cpu_driver.h"

#define SDL3_DESKTOP_WIDTH  1280
#define SDL3_DESKTOP_HEIGHT 800
#define SDL3_MENUBAR_H      28
#define SDL3_DOCK_H         80

typedef struct {
    int x, y, w, h;
    bool visible;
    bool focused;
    int z_order;
} SDL3_Window;

typedef struct {
    SDL_Window *sdl_window;
    SDL_Renderer *renderer;
    SDL_Surface *framebuffer;
    SDL_Cursor *cursor;

    int mouse_x, mouse_y;
    int mouse_buttons;
    bool running;

    SDL3_Window *windows;
    int num_windows;
    int max_windows;

    SDL3_CPU_Driver cpu_driver_amd;
    SDL3_CPU_Driver cpu_driver_intel;
} SDL3_Desktop;

bool sdl3_desktop_init(SDL3_Desktop *desktop);
void sdl3_desktop_run(SDL3_Desktop *desktop);
void sdl3_desktop_quit(SDL3_Desktop *desktop);
void sdl3_desktop_render(SDL3_Desktop *desktop);
void sdl3_desktop_handle_events(SDL3_Desktop *desktop);

void sdl3_logo_render(SDL_Renderer *renderer, int x, int y, int w, int h);

#endif /* SDL3_DESKTOP_H */
