#include "sdl3_desktop.h"
#include "sdl3_cpu_driver.h"
#include <string.h>

static uint32_t pixel_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fill_rect(SDL_Renderer *r, int x, int y, int w, int h, uint32_t color) {
    SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};
    SDL_SetRenderDrawColorFloat(r,
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 8) & 0xFF) / 255.0f,
        (color & 0xFF) / 255.0f,
        ((color >> 24) & 0xFF) / 255.0f);
    SDL_RenderFillRect(r, &rect);
}

bool sdl3_desktop_init(SDL3_Desktop *desktop) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    desktop->sdl_window = SDL_CreateWindow("CodeOS Desktop",
        SDL3_DESKTOP_WIDTH, SDL3_DESKTOP_HEIGHT,
        SDL_WINDOW_RESIZABLE);
    if (!desktop->sdl_window) {
        SDL_Quit();
        return false;
    }

    desktop->renderer = SDL_CreateRenderer(desktop->sdl_window, NULL);
    if (!desktop->renderer) {
        SDL_DestroyWindow(desktop->sdl_window);
        SDL_Quit();
        return false;
    }

    desktop->framebuffer = SDL_CreateSurface(SDL3_DESKTOP_WIDTH, SDL3_DESKTOP_HEIGHT, SDL_PIXELFORMAT_XRGB8888);
    if (!desktop->framebuffer) {
        SDL_DestroyRenderer(desktop->renderer);
        SDL_DestroyWindow(desktop->sdl_window);
        SDL_Quit();
        return false;
    }

    desktop->cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    SDL_SetCursor(desktop->cursor);

    desktop->mouse_x = SDL3_DESKTOP_WIDTH / 2;
    desktop->mouse_y = SDL3_DESKTOP_HEIGHT / 2;
    desktop->mouse_buttons = 0;
    desktop->running = true;
    desktop->num_windows = 0;
    desktop->max_windows = 16;
    desktop->windows = (SDL3_Window *)SDL_malloc(desktop->max_windows * sizeof(SDL3_Window));

    sdl3_cpu_driver_init(&desktop->cpu_driver_amd);
    sdl3_cpu_driver_init(&desktop->cpu_driver_intel);

    return true;
}

void sdl3_desktop_quit(SDL3_Desktop *desktop) {
    if (desktop->cursor) SDL_DestroyCursor(desktop->cursor);
    if (desktop->framebuffer) SDL_DestroySurface(desktop->framebuffer);
    if (desktop->renderer) SDL_DestroyRenderer(desktop->renderer);
    if (desktop->sdl_window) SDL_DestroyWindow(desktop->sdl_window);
    SDL_Quit();
    if (desktop->windows) SDL_free(desktop->windows);
}

void sdl3_desktop_handle_events(SDL3_Desktop *desktop) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            desktop->running = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                desktop->running = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            desktop->mouse_x = (int)event.motion.x;
            desktop->mouse_y = (int)event.motion.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) desktop->mouse_buttons |= 1;
            else if (event.button.button == SDL_BUTTON_RIGHT) desktop->mouse_buttons |= 2;
            else if (event.button.button == SDL_BUTTON_MIDDLE) desktop->mouse_buttons |= 4;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) desktop->mouse_buttons &= ~1;
            else if (event.button.button == SDL_BUTTON_RIGHT) desktop->mouse_buttons &= ~2;
            else if (event.button.button == SDL_BUTTON_MIDDLE) desktop->mouse_buttons &= ~4;
            break;
        default:
            break;
        }
    }
}

static void draw_cursor(SDL_Surface *fb, int cx, int cy) {
    int w = 32, h = 32;
    uint32_t *pixels = (uint32_t *)fb->pixels;
    int pitch = fb->pitch / 4;
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int px = cx + dx - w/2;
            int py = cy + dy - h/2;
            if (px < 0 || px >= SDL3_DESKTOP_WIDTH || py < 0 || py >= SDL3_DESKTOP_HEIGHT) continue;
            int dist_x = dx - w/2;
            int dist_y = dy - h/2;
            int in_cursor = (dist_x * dist_x + dist_y * dist_y) < (w/2) * (w/2);
            int in_hotspot = (dx >= w/2 - 2 && dx <= w/2 + 2 && dy >= h/2 - 2 && dy <= h/2 + 2);
            uint32_t color = in_hotspot ? 0xFFFF0000 : (in_cursor ? 0xFFFFFFFF : 0xFF000000);
            if (in_cursor || in_hotspot) {
                pixels[py * pitch + px] = color;
            }
        }
    }
}

void sdl3_desktop_render(SDL3_Desktop *desktop) {
    SDL_Surface *fb = desktop->framebuffer;
    if (!fb) return;

    uint32_t *pixels = (uint32_t *)fb->pixels;
    int pitch = fb->pitch / 4;

    for (int y = 0; y < SDL3_DESKTOP_HEIGHT; y++) {
        float t = (float)y / (float)SDL3_DESKTOP_HEIGHT;
        uint8_t r = (uint8_t)(37 + (31 - 37) * t);
        uint8_t g = (uint8_t)(37 + (31 - 37) * t);
        uint8_t b = (uint8_t)(51 + (44 - 51) * t);
        uint32_t color = pixel_argb(0xFF, r, g, b);
        for (int x = 0; x < SDL3_DESKTOP_WIDTH; x++) {
            pixels[y * pitch + x] = color;
        }
    }

    fill_rect(desktop->renderer, 0, 0, SDL3_DESKTOP_WIDTH, SDL3_MENUBAR_H, 0xFF1A1A1E);

    sdl3_logo_render(desktop->renderer, 8, 4, 20, 20);

    fill_rect(desktop->renderer, 35, 8, 150, 12, 0xFF2C2C30);
    fill_rect(desktop->renderer, SDL3_DESKTOP_WIDTH - 130, 6, 120, 16, 0xFF2C2C30);

    int dock_y = SDL3_DESKTOP_HEIGHT - SDL3_DOCK_H;
    fill_rect(desktop->renderer, 0, dock_y, SDL3_DESKTOP_WIDTH, SDL3_DOCK_H, 0xFF1A1A1E);
    fill_rect(desktop->renderer, 0, dock_y, SDL3_DESKTOP_WIDTH, 1, 0xFF3A3A3E);

    int icon_size = 48;
    int icon_spacing = 60;
    int icon_start_x = (SDL3_DESKTOP_WIDTH - (icon_size * 5 + icon_spacing * 4)) / 2;
    uint32_t icon_colors[] = {
        0xFF4A90D9, 0xFF50C878, 0xFFFF6B6B, 0xFFFFD93D, 0xFF9B59B6
    };
    for (int i = 0; i < 5; i++) {
        int ix = icon_start_x + i * (icon_size + icon_spacing);
        int iy = dock_y + (SDL3_DOCK_H - icon_size) / 2;
        fill_rect(desktop->renderer, ix, iy, icon_size, icon_size, icon_colors[i]);
        fill_rect(desktop->renderer, ix + 4, iy + 4, icon_size - 8, icon_size - 8, icon_colors[i] | 0x40000000);
    }

    sdl3_cpu_driver_render_info(&desktop->cpu_driver_amd, SDL3_DESKTOP_WIDTH - 260, dock_y - 55, 250, 45);

    draw_cursor(fb, desktop->mouse_x, desktop->mouse_y);

    SDL_Texture *tex = SDL_CreateTextureFromSurface(desktop->renderer, fb);
    if (tex) {
        SDL_FRect dst = {0.0f, 0.0f, (float)SDL3_DESKTOP_WIDTH, (float)SDL3_DESKTOP_HEIGHT};
        SDL_RenderTexture(desktop->renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }

    SDL_RenderPresent(desktop->renderer);
}

void sdl3_desktop_run(SDL3_Desktop *desktop) {
    while (desktop->running) {
        sdl3_desktop_handle_events(desktop);
        sdl3_desktop_render(desktop);
        SDL_Delay(16);
    }
}

void sdl3_logo_render(SDL_Renderer *renderer, int x, int y, int w, int h) {
    fill_rect(renderer, x, y, w, h, 0xFF4A90D9);
    fill_rect(renderer, x + 3, y + 3, w - 6, h - 6, 0xFF3A7BC0);
}
