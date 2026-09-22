#ifndef X11_SERVER_H
#define X11_SERVER_H

#include <stdint.h>

#define X11_MAX_CLIENTS 8
#define X11_MAX_WINDOWS 32
#define X11_MAX_ATOMS 256
#define X11_BUF_SIZE 4096

typedef enum {
    X11_REQ_CREATE_WINDOW = 1,
    X11_REQ_DESTROY_WINDOW = 2,
    X11_REQ_MAP_WINDOW = 3,
    X11_REQ_UNMAP_WINDOW = 4,
    X11_REQ_CONFIGURE_WINDOW = 5,
    X11_REQ_GET_GEOMETRY = 6,
    X11_REQ_QUERY_TREE = 7,
    X11_REQ_INTERN_ATOM = 8,
    X11_REQ_GET_ATOM_NAME = 9,
    X11_REQ_CHANGE_PROPERTY = 10,
    X11_REQ_DELETE_PROPERTY = 11,
    X11_REQ_GET_PROPERTY = 12,
    X11_REQ_LIST_PROPERTIES = 13,
    X11_REQ_SET_SELECTION_OWNER = 14,
    X11_REQ_GET_SELECTION_OWNER = 15,
    X11_REQ_CONVERT_SELECTION = 16,
    X11_REQ_GRAB_KEYBOARD = 17,
    X11_REQ_UNGRAB_KEYBOARD = 18,
    X11_REQ_GRAB_POINTER = 19,
    X11_REQ_UNGRAB_POINTER = 20,
    X11_REQ_WARP_POINTER = 21,
    X11_REQ_CHANGE_ACTIVE_POINTER_GRAB = 22,
    X11_REQ_SEND_EVENT = 23,
    X11_REQ_GRAB_SERVER = 24,
    X11_REQ_UNGRAB_SERVER = 25,
    X11_REQ_QUERY_POINTER = 26,
    X11_REQ_GET_MOTION_EVENTS = 27,
    X11_REQ_TRANSLATE_COORDS = 28,
    X11_REQ_QUERY_KEYMAP = 29,
    X11_REQ_OPEN_FONT = 30,
    X11_REQ_CLOSE_FONT = 31,
    X11_REQ_QUERY_FONT = 32,
    X11_REQ_QUERY_TEXT_EXTENTS = 33,
    X11_REQ_LIST_FONTS = 34,
    X11_REQ_LIST_FONTS_WITH_INFO = 35,
    X11_REQ_SET_FONT_PATH = 36,
    X11_REQ_GET_FONT_PATH = 37,
    X11_REQ_CREATE_PIXMAP = 38,
    X11_REQ_FREE_PIXMAP = 39,
    X11_REQ_CREATE_GC = 40,
    X11_REQ_CHANGE_GC = 41,
    X11_REQ_COPY_GC = 42,
    X11_REQ_SET_DASHES = 43,
    X11_REQ_SET_CLIP_RECTANGLES = 44,
    X11_REQ_FREE_GC = 45,
    X11_REQ_CLEAR_AREA = 46,
    X11_REQ_COPY_AREA = 47,
    X11_REQ_COPY_PLANE = 48,
    X11_REQ_POLY_POINT = 49,
    X11_REQ_POLY_LINE = 50,
    X11_REQ_POLY_SEGMENT = 51,
    X11_REQ_POLY_RECTANGLE = 52,
    X11_REQ_POLY_ARC = 53,
    X11_REQ_FILL_POLY = 54,
    X11_REQ_POLY_FILL_RECTANGLE = 55,
    X11_REQ_POLY_FILL_ARC = 56,
    X11_REQ_PUT_IMAGE = 57,
    X11_REQ_GET_IMAGE = 58,
    X11_REQ_POLY_TEXT_8 = 59,
    X11_REQ_POLY_TEXT_16 = 60,
    X11_REQ_IMAGE_TEXT_8 = 61,
    X11_REQ_IMAGE_TEXT_16 = 62,
    X11_REQ_CREATE_COLORMAP = 63,
    X11_REQ_FREE_COLORMAP = 64,
    X11_REQ_COPY_COLORMAP_AND_FREE = 65,
    X11_REQ_INSTALL_COLORMAP = 66,
    X11_REQ_UNINSTALL_COLORMAP = 67,
    X11_REQ_LIST_INSTALLED_COLORMAPS = 68,
    X11_REQ_ALLOC_COLOR = 69,
    X11_REQ_ALLOC_NAMED_COLOR = 70,
    X11_REQ_ALLOC_COLOR_CELLS = 71,
    X11_REQ_ALLOC_COLOR_PLANES = 72,
    X11_REQ_FREE_COLORS = 73,
    X11_REQ_STORE_COLORS = 74,
    X11_REQ_STORE_NAMED_COLOR = 75,
    X11_REQ_QUERY_COLORS = 76,
    X11_REQ_LOOKUP_COLOR = 77,
    X11_REQ_CREATE_CURSOR = 78,
    X11_REQ_CREATE_GLYPH_CURSOR = 79,
    X11_REQ_FREE_CURSOR = 80,
    X11_REQ_RECOLOR_CURSOR = 81,
    X11_REQ_QUERY_BEST_SIZE = 82,
    X11_REQ_QUERY_EXTENSION = 83,
    X11_REQ_LIST_EXTENSIONS = 84,
    X11_REQ_CHANGE_KEYBOARD_MAPPING = 85,
    X11_REQ_GET_KEYBOARD_MAPPING = 86,
    X11_REQ_CHANGE_KEYBOARD_CONTROL = 87,
    X11_REQ_GET_KEYBOARD_CONTROL = 88,
    X11_REQ_BELL = 89,
    X11_REQ_CHANGE_POINTER_CONTROL = 90,
    X11_REQ_GET_POINTER_CONTROL = 91,
    X11_REQ_SET_SCREEN_SAVER = 92,
    X11_REQ_GET_SCREEN_SAVER = 93,
    X11_REQ_CHANGE_HOSTS = 94,
    X11_REQ_LIST_HOSTS = 95,
    X11_REQ_SET_ACCESS_CONTROL = 96,
    X11_REQ_SET_CLOSE_DOWN_MODE = 97,
    X11_REQ_KILL_CLIENT = 98,
    X11_REQ_ROTATE_PROPERTIES = 99,
    X11_REQ_FORCE_SCREEN_SAVER = 100,
    X11_REQ_SET_POINTER_MAPPING = 101,
    X11_REQ_GET_POINTER_MAPPING = 102,
    X11_REQ_SET_MODIFIER_MAPPING = 103,
    X11_REQ_GET_MODIFIER_MAPPING = 104,
    X11_REQ_NO_OPERATION = 127,
} x11_request_t;

typedef enum {
    X11_EVENT_KEY_PRESS = 2,
    X11_EVENT_KEY_RELEASE = 3,
    X11_EVENT_BUTTON_PRESS = 4,
    X11_EVENT_BUTTON_RELEASE = 5,
    X11_EVENT_MOTION_NOTIFY = 6,
    X11_EVENT_ENTER_NOTIFY = 7,
    X11_EVENT_LEAVE_NOTIFY = 8,
    X11_EVENT_FOCUS_IN = 9,
    X11_EVENT_FOCUS_OUT = 10,
    X11_EVENT_KEYMAP_NOTIFY = 11,
    X11_EVENT_EXPOSE = 12,
    X11_EVENT_GRAPHICS_EXPOSURE = 13,
    X11_EVENT_NO_EXPOSURE = 14,
    X11_EVENT_VISIBILITY_NOTIFY = 15,
    X11_EVENT_CREATE_NOTIFY = 16,
    X11_EVENT_DESTROY_NOTIFY = 17,
    X11_EVENT_UNMAP_NOTIFY = 18,
    X11_EVENT_MAP_NOTIFY = 19,
    X11_EVENT_MAP_REQUEST = 20,
    X11_EVENT_REPARENT_NOTIFY = 21,
    X11_EVENT_CONFIGURE_NOTIFY = 22,
    X11_EVENT_CONFIGURE_REQUEST = 23,
    X11_EVENT_GRAVITY_NOTIFY = 24,
    X11_EVENT_RESIZE_REQUEST = 25,
    X11_EVENT_CIRCULATE_NOTIFY = 26,
    X11_EVENT_CIRCULATE_REQUEST = 27,
    X11_EVENT_PROPERTY_NOTIFY = 28,
    X11_EVENT_SELECTION_CLEAR = 29,
    X11_EVENT_SELECTION_REQUEST = 30,
    X11_EVENT_SELECTION_NOTIFY = 31,
    X11_EVENT_COLORMAP_NOTIFY = 32,
    X11_EVENT_CLIENT_MESSAGE = 33,
    X11_EVENT_MAPPING_NOTIFY = 34,
    X11_EVENT_GENERIC = 35,
} x11_event_type_t;

typedef struct {
    uint8_t type;
    uint8_t detail;
    uint16_t sequence;
    uint32_t time;
    uint32_t root;
    uint32_t event;
    uint32_t child;
    int16_t root_x, root_y;
    int16_t event_x, event_y;
    uint16_t state;
    uint8_t same_screen;
    uint8_t pad[31];
} x11_event_t;

typedef struct {
    int id;
    int client_id;
    uint32_t xid;
    int x, y;
    int width, height;
    int border_width;
    int depth;
    uint32_t visual;
    uint32_t parent;
    int mapped;
    int class;
    int override_redirect;
    char title[64];
} x11_window_t;

typedef struct {
    int id;
    int fd;
    int pid;
    uint32_t seq;
    uint8_t req_buf[X11_BUF_SIZE];
    int req_len;
    uint8_t reply_buf[X11_BUF_SIZE];
    int reply_len;
    int authenticated;
    int big_endian;
    int byte_order;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t *atoms;
    int atom_count;
    x11_window_t *windows;
    int window_count;
    uint32_t root_window;
} x11_client_t;

void x11_server_init(void);
void x11_server_tick(void);

int x11_client_connect(int fd, int pid);
void x11_client_disconnect(int client_id);
int x11_client_process_request(int client_id, const uint8_t *data, int len, uint8_t *reply, int *reply_len);

void x11_send_event(int client_id, const x11_event_t *event);
void x11_broadcast_event(const x11_event_t *event, int exclude_client);

x11_client_t *x11_get_client(int id);
x11_window_t *x11_get_window(uint32_t xid);

uint32_t x11_create_kernel_window(const char *title, int w, int h);
void x11_release_window(uint32_t xid);
uint32_t x11_window_xid(int index);

uint32_t x11_intern_atom(const char *name, int only_if_exists);
const char *x11_get_atom_name(uint32_t atom);

#endif