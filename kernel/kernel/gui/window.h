#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"

typedef struct window {
    int x, y, w, h;
    uint32_t fg;
    uint32_t bg;
    int visible;
    int focused;
    char title[128];
} window_t;

#endif
