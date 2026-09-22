/* CodeOS Qt6 Test Application
 * Simple window that draws a colored rectangle and handles input
 * Demonstrates the Qt6 platform integration for CodeOS */

#include "codeos_platform.h"
#include <stdint.h>

/* Simple string functions */
static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* Main entry point */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Initialize platform */
    if (codeos_platform_init() < 0) {
        return -1;
    }

    /* Get framebuffer info */
    codeos_fb_info_t fb;
    if (codeos_platform_get_fb_info(&fb) < 0) {
        return -1;
    }

    /* Create a window */
    int win_w = 640;
    int win_h = 480;
    int win_x = (fb.width - win_w) / 2;
    int win_y = (fb.height - win_h) / 2;
    uint64_t window = codeos_window_create(win_x, win_y, win_w, win_h,
                                           "CodeOS Qt6 Test");

    if (!window) {
        return -1;
    }

    /* Show the window */
    codeos_window_show(window);

    /* Main event loop */
    int running = 1;
    int mouse_x = win_w / 2;
    int mouse_y = win_h / 2;
    int mouse_buttons = 0;
    int color_r = 100;
    int color_g = 150;
    int color_b = 200;
    int frame_count = 0;

    while (running) {
        /* Poll input events */
        codeos_input_event_t ev;
        while (codeos_platform_poll_input(&ev)) {
            if (ev.type == 1) {  /* Keyboard */
                if (ev.key == 27) {  /* Escape */
                    running = 0;
                }
            } else if (ev.type == 2) {  /* Mouse */
                mouse_x = ev.mouse_x - win_x;
                mouse_y = ev.mouse_y - win_y;
                mouse_buttons = ev.mouse_buttons;
            }
        }

        /* Clear window with gradient background */
        codeos_draw_gradient_v(window, 0, 0, win_w, win_h,
                              CODEOS_COLOR(40, 44, 52),
                              CODEOS_COLOR(33, 37, 43));

        /* Draw title bar */
        codeos_fill_rect(window, 0, 0, win_w, 32,
                        CODEOS_COLOR(50, 54, 62));
        codeos_draw_text(window, 10, 8, "CodeOS Qt6 Test Window",
                        CODEOS_COLOR(220, 223, 228), 16);

        /* Draw traffic light buttons */
        codeos_fill_rounded_rect(window, 12, 10, 12, 12, 6,
                                CODEOS_COLOR(255, 95, 86));  /* Close */
        codeos_fill_rounded_rect(window, 30, 10, 12, 12, 6,
                                CODEOS_COLOR(255, 189, 46));  /* Minimize */
        codeos_fill_rounded_rect(window, 48, 10, 12, 12, 6,
                                CODEOS_COLOR(39, 201, 63));   /* Maximize */

        /* Draw a colored square that follows the mouse */
        int square_size = 50;
        int square_x = mouse_x - square_size / 2;
        int square_y = mouse_y - square_size / 2;

        /* Animate color based on frame count */
        color_r = (frame_count * 2) % 256;
        color_g = (frame_count * 3) % 256;
        color_b = (frame_count * 5) % 256;

        /* Draw the square with shadow */
        codeos_draw_shadow(window, square_x + 3, square_y + 3,
                          square_size, square_size, 8,
                          CODEOS_COLOR(0, 0, 0), 4);
        codeos_fill_rounded_rect(window, square_x, square_y,
                                square_size, square_size, 8,
                                CODEOS_COLOR(color_r, color_g, color_b));

        /* Draw mouse position text */
        char pos_text[64];
        int pos = 0;
        pos_text[pos++] = 'X';
        pos_text[pos++] = ':';
        int val = mouse_x;
        if (val < 0) { pos_text[pos++] = '-'; val = -val; }
        char tmp[10];
        int tpos = 0;
        if (val == 0) tmp[tpos++] = '0';
        while (val > 0) { tmp[tpos++] = '0' + (val % 10); val /= 10; }
        for (int i = tpos - 1; i >= 0; i--) pos_text[pos++] = tmp[i];
        pos_text[pos++] = ' ';
        pos_text[pos++] = 'Y';
        pos_text[pos++] = ':';
        val = mouse_y;
        if (val < 0) { pos_text[pos++] = '-'; val = -val; }
        tpos = 0;
        if (val == 0) tmp[tpos++] = '0';
        while (val > 0) { tmp[tpos++] = '0' + (val % 10); val /= 10; }
        for (int i = tpos - 1; i >= 0; i--) pos_text[pos++] = tmp[i];
        pos_text[pos] = 0;

        codeos_draw_text(window, 10, win_h - 30, pos_text,
                        CODEOS_COLOR(171, 178, 191), 14);

        /* Draw instruction text */
        codeos_draw_text(window, 10, win_h - 50,
                        "Press ESC to exit | Move mouse to interact",
                        CODEOS_COLOR(171, 178, 191), 14);

        /* Composite to screen */
        codeos_composite(window);

        /* Frame delay */
        codeos_platform_sleep_ms(16);  /* ~60fps */
        frame_count++;
    }

    /* Cleanup */
    codeos_window_destroy(window);
    codeos_platform_cleanup();

    return 0;
}
