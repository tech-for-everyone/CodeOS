#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <stdint.h>
#include <string.h>

#define SCREEN_WIDTH  1280
#define SCREEN_HEIGHT 800

static SDL_Window *g_window = NULL;
static SDL_Renderer *g_renderer = NULL;

static bool init(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }

    g_window = SDL_CreateWindow("CodeOS SDL3 Demo",
                                SCREEN_WIDTH, SCREEN_HEIGHT,
                                SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, NULL);
    if (!g_renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        return false;
    }

    return true;
}

static void cleanup(void)
{
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
    }
    SDL_Quit();
}

static bool handle_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            return false;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                return false;
            }
            break;
        default:
            break;
        }
    }
    return true;
}

static void render_frame(void)
{
    SDL_SetRenderDrawColorFloat(g_renderer, 23.0f/255.0f, 23.0f/255.0f, 40.0f/255.0f, 1.0f);
    SDL_RenderClear(g_renderer);

    SDL_SetRenderDrawColorFloat(g_renderer, 45.0f/255.0f, 45.0f/255.0f, 70.0f/255.0f, 1.0f);
    SDL_FRect menubar = {0.0f, 0.0f, (float)SCREEN_WIDTH, 28.0f};
    SDL_RenderFillRect(g_renderer, &menubar);

    SDL_SetRenderDrawColorFloat(g_renderer, 137.0f/255.0f, 180.0f/255.0f, 250.0f/255.0f, 1.0f);
    SDL_FRect dock = {0.0f, (float)(SCREEN_HEIGHT - 80), (float)SCREEN_WIDTH, 80.0f};
    SDL_RenderFillRect(g_renderer, &dock);

    SDL_SetRenderDrawColorFloat(g_renderer, 245.0f/255.0f, 245.0f/255.0f, 247.0f/255.0f, 1.0f);
    SDL_FRect title = {10.0f, 6.0f, 200.0f, 16.0f};
    SDL_RenderFillRect(g_renderer, &title);

    SDL_RenderPresent(g_renderer);
}

int SDL_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (!init()) {
        return 1;
    }

    bool running = true;
    while (running) {
        running = handle_events();
        render_frame();
    }

    cleanup();
    return 0;
}
