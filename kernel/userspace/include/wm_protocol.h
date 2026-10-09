#ifndef WM_PROTOCOL_H
#define WM_PROTOCOL_H

#include <stdint.h>

#define WM_PROTO_VERSION 2

#define WM_PIPE_CMD  3
#define WM_PIPE_EVENT 4

enum wm_msg_type {
    WM_NOP        = 0x00,
    WM_CREATE_WIN = 0x01,
    WM_CLOSE_WIN  = 0x02,
    WM_RESIZE_WIN = 0x03,
    WM_MINIMIZE   = 0x04,
    WM_MAXIMIZE   = 0x05,
    WM_MOVE_WIN   = 0x06,

    WM_FILL_RECT  = 0x10,
    WM_DRAW_STR   = 0x11,
    WM_CLEAR      = 0x12,
    WM_SCROLL     = 0x13,
    WM_FLUSH      = 0x15,

    WM_EVENT_KEY    = 0x80,
    WM_EVENT_MOUSE  = 0x81,
    WM_EVENT_FOCUS  = 0x82,
    WM_EVENT_CLOSED = 0x83,
    WM_EVENT_CONN   = 0x84,
    WM_EVENT_RESIZE = 0x85,
    WM_EVENT_MINIMIZE = 0x86,
    WM_EVENT_MAXIMIZE = 0x87,
};

#define WM_WIN_ID_NONE 0xFF

#endif
