#ifndef WIDGETS_H
#define WIDGETS_H

#include "types.h"

typedef struct widget {
    int x, y, w, h;
    uint32_t color;
    int type;
} widget_t;

#endif
