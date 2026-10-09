#ifndef APPVM_H
#define APPVM_H

#include <stdint.h>
#include "elf.h"

#define APPVM_MAX_APPS      32
#define APPVM_NAME_MAX      48
#define APPVM_PATH_MAX      128
#define APPVM_WIN32_THUNKS  64

typedef enum {
    APPVM_TYPE_ELF,
    APPVM_TYPE_PE,
    APPVM_TYPE_APK,
} appvm_type_t;

typedef struct {
    char name[APPVM_NAME_MAX];
    char path[APPVM_PATH_MAX];
    appvm_type_t type;
    int  win;
    int  pid;
    int  x, y, w, h;
    int  visible;
    char title[64];
    int  status;
    char status_msg[128];
    int  pe_section_count;

    void *base_addr;
    int   image_size;
    uint64_t entry_point;
    uint64_t stack_top;
    elf_auxv_info_t auxv;

    uint32_t pe_entry;
    uint32_t pe_image_base;
    uint32_t pe_size;
} appvm_app_t;

int  appvm_init(void);
int  appvm_load(const char *name, const char *path);
int  appvm_launch(int idx);
void appvm_close(int idx);
int  appvm_is_running(int idx);
void appvm_draw_all(void);
int  appvm_click(int mx, int my);
void appvm_key(int key);
int  appvm_app_count(void);
appvm_app_t *appvm_get_app(int idx);
int  appvm_find_by_name(const char *name);

#endif
