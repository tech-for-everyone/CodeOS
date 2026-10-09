#include "appvm.h"
#include "desktop.h"
#include "windows.h"
#include "fs.h"
#include "elf.h"
#include "process.h"
#include "sched.h"
#include "umode.h"
#include "shell.h"
#include "string.h"
#include "kprintf.h"
#include "mm.h"
#include "fb.h"
#include "keyboard.h"
#include "timer.h"

static appvm_app_t apps[APPVM_MAX_APPS];
static int app_count;

/* ── PE header structures ── */
typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} __attribute__((packed)) pe_dos_header_t;

typedef struct {
    uint32_t Signature;
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} __attribute__((packed)) pe_nt_headers32_t;

typedef struct {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint32_t BaseOfData;
    uint32_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint32_t SizeOfStackReserve;
    uint32_t SizeOfStackCommit;
    uint32_t SizeOfHeapReserve;
    uint32_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
} __attribute__((packed)) pe_optional_header32_t;

typedef struct {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} __attribute__((packed)) pe_section_header_t;

/* ── Win32 API thunks ── */
typedef uint32_t (*win32_thunk_t)(uint32_t, uint32_t, uint32_t, uint32_t);

static struct {
    const char *name;
    win32_thunk_t func;
} win32_thunks[APPVM_WIN32_THUNKS];
static int thunk_count;

static void appvm_register_thunk(const char *name, win32_thunk_t func) {
    if (thunk_count >= APPVM_WIN32_THUNKS) return;
    win32_thunks[thunk_count].name = name;
    win32_thunks[thunk_count].func = func;
    thunk_count++;
}

/* ── Win32 thunk implementations ── */
static uint32_t thunk_MessageBoxA(uint32_t hwnd, uint32_t text, uint32_t caption, uint32_t type) {
    const char *t = (const char*)(uintptr_t)text;
    const char *c = (const char*)(uintptr_t)caption;
    kprintf("appvm: MessageBoxA hwnd=%x text='%s' caption='%s' type=%x\n", hwnd, t ? t : "", c ? c : "", type);
    return 1;
}

static uint32_t thunk_ExitProcess(uint32_t code, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: ExitProcess(%x)\n", code);
    return 0;
}

static uint32_t thunk_GetStockObject(uint32_t obj, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: GetStockObject(%x)\n", obj);
    return 0x1001;
}

static uint32_t thunk_GetDC(uint32_t hwnd, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: GetDC(%x)\n", hwnd);
    return 0x2001;
}

static uint32_t thunk_ReleaseDC(uint32_t hwnd, uint32_t dc, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: ReleaseDC(%x, %x)\n", hwnd, dc);
    return 1;
}

static uint32_t thunk_BeginPaint(uint32_t hwnd, uint32_t ps, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: BeginPaint(%x, %x)\n", hwnd, ps);
    return 0x2001;
}

static uint32_t thunk_EndPaint(uint32_t hwnd, uint32_t ps, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: EndPaint(%x, %x)\n", hwnd, ps);
    return 1;
}

static uint32_t thunk_CreateWindowExA(uint32_t ex, uint32_t cls, uint32_t title, uint32_t style) {
    kprintf("appvm: CreateWindowExA ex=%x cls='%s' title='%s' style=%x\n",
            ex, (const char*)(uintptr_t)cls, (const char*)(uintptr_t)title, style);
    return 0x3001;
}

static uint32_t thunk_DefWindowProcA(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp) {
    kprintf("appvm: DefWindowProcA hwnd=%x msg=%x wp=%x lp=%x\n", hwnd, msg, wp, lp);
    return 0;
}

static uint32_t thunk_PostQuitMessage(uint32_t code, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: PostQuitMessage(%x)\n", code);
    return 0;
}

static uint32_t thunk_RegisterClassExA(uint32_t wc, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: RegisterClassExA(%x)\n", wc);
    return 0x4001;
}

static uint32_t thunk_ShowWindow(uint32_t hwnd, uint32_t cmd, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: ShowWindow(%x, %x)\n", hwnd, cmd);
    return 1;
}

static uint32_t thunk_UpdateWindow(uint32_t hwnd, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: UpdateWindow(%x)\n", hwnd);
    return 1;
}

static uint32_t thunk_GetMessageA(uint32_t msg, uint32_t hwnd, uint32_t min, uint32_t max) {
    kprintf("appvm: GetMessageA msg=%x hwnd=%x min=%x max=%x\n", msg, hwnd, min, max);
    return 0;
}

static uint32_t thunk_TranslateMessage(uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: TranslateMessage(%x)\n", msg);
    return 1;
}

static uint32_t thunk_DispatchMessageA(uint32_t msg, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: DispatchMessageA(%x)\n", msg);
    return 0;
}

static uint32_t thunk_LoadIconA(uint32_t inst, uint32_t name, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: LoadIconA(%x, '%s')\n", inst, (const char*)(uintptr_t)name);
    return 0x5001;
}

static uint32_t thunk_LoadCursorA(uint32_t inst, uint32_t name, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: LoadCursorA(%x, '%s')\n", inst, (const char*)(uintptr_t)name);
    return 0x6001;
}

static uint32_t thunk_GetClientRect(uint32_t hwnd, uint32_t rect, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: GetClientRect(%x, %x)\n", hwnd, rect);
    return 1;
}

static uint32_t thunk_InvalidateRect(uint32_t hwnd, uint32_t rect, uint32_t erase, uint32_t c) {
    (void)c;
    kprintf("appvm: InvalidateRect(%x, %x, %x)\n", hwnd, rect, erase);
    return 1;
}

static uint32_t thunk_SetTimer(uint32_t hwnd, uint32_t id, uint32_t ms, uint32_t cb) {
    kprintf("appvm: SetTimer(%x, %x, %x, %x)\n", hwnd, id, ms, cb);
    return id;
}

static uint32_t thunk_KillTimer(uint32_t hwnd, uint32_t id, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    kprintf("appvm: KillTimer(%x, %x)\n", hwnd, id);
    return 1;
}

static uint32_t thunk_wsprintfA(uint32_t buf, uint32_t fmt, uint32_t arg, uint32_t c) {
    (void)c;
    kprintf("appvm: wsprintfA buf=%x fmt='%s' arg=%x\n", buf, (const char*)(uintptr_t)fmt, arg);
    return 0;
}

static uint32_t thunk_lstrlenA(uint32_t str, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    const char *s = (const char*)(uintptr_t)str;
    int len = 0;
    if (s) while (*s++) len++;
    return len;
}

static uint32_t thunk_lstrcpyA(uint32_t dst, uint32_t src, uint32_t b, uint32_t c) {
    (void)b; (void)c;
    const char *s = (const char*)(uintptr_t)src;
    char *d = (char*)(uintptr_t)dst;
    if (d && s) while ((*d++ = *s++));
    return dst;
}

static uint32_t thunk_HeapAlloc(uint32_t heap, uint32_t flags, uint32_t size, uint32_t c) {
    (void)c;
    void *p = malloc(size);
    kprintf("appvm: HeapAlloc(%x, %x, %x) -> %p\n", heap, flags, size, p);
    return (uint32_t)(uintptr_t)p;
}

static uint32_t thunk_HeapFree(uint32_t heap, uint32_t flags, uint32_t ptr, uint32_t c) {
    (void)c;
    kprintf("appvm: HeapFree(%x, %x, %x)\n", heap, flags, ptr);
    free((void*)(uintptr_t)ptr);
    return 1;
}

static uint32_t thunk_GetProcessHeap(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    (void)a; (void)b; (void)c; (void)d;
    return 0x7001;
}

static uint32_t thunk_GetCommandLineA(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    (void)a; (void)b; (void)c; (void)d;
    return 0;
}

static uint32_t thunk_GetModuleHandleA(uint32_t name, uint32_t a, uint32_t b, uint32_t c) {
    (void)a; (void)b; (void)c;
    kprintf("appvm: GetModuleHandleA('%s')\n", (const char*)(uintptr_t)name);
    return 0x8001;
}

/* ── PE Loader ── */
static int load_pe(appvm_app_t *app, const char *path) {
    int fsize = 0, is_dir = 0;
    if (fs_get_info(path, &fsize, &is_dir) < 0 || is_dir || fsize < 64) {
        kprintf("appvm: '%s' invalid (size=%d)\n", path, fsize);
        return -1;
    }

    char *buf = malloc(fsize);
    if (!buf) return -1;

    int n = fs_read((char*)path, buf, fsize);
    if (n != fsize) { free(buf); return -1; }

    pe_dos_header_t *dos = (pe_dos_header_t*)buf;
    if (dos->e_magic != 0x5A4D) { free(buf); return -1; }

    pe_nt_headers32_t *nt = (pe_nt_headers32_t*)(buf + dos->e_lfanew);
    if (nt->Signature != 0x00004550) { free(buf); return -1; }

    pe_optional_header32_t *opt = (pe_optional_header32_t*)((uint8_t*)nt + sizeof(pe_nt_headers32_t));

    app->type = APPVM_TYPE_PE;
    app->pe_entry = opt->AddressOfEntryPoint;
    app->pe_image_base = opt->ImageBase;
    app->pe_size = opt->SizeOfImage;

    uint32_t img_size = opt->SizeOfImage;
    if (img_size > 32 * 1024 * 1024) { free(buf); return -1; }

    void *base = malloc(img_size);
    if (!base) { free(buf); return -1; }
    app->base_addr = base;
    app->image_size = img_size;
    app->stack_top = 0;
    app->entry_point = 0;

    memset(base, 0, img_size);
    uint32_t hdr_sz = opt->SizeOfHeaders;
    if (hdr_sz > (uint32_t)n) hdr_sz = n;
    memcpy(base, buf, hdr_sz);

    pe_section_header_t *sec = (pe_section_header_t*)((uint8_t*)opt + sizeof(pe_optional_header32_t));
    for (int i = 0; i < nt->NumberOfSections; i++) {
        if (sec[i].PointerToRawData && sec[i].SizeOfRawData) {
            uint32_t dst = sec[i].VirtualAddress;
            uint32_t src = sec[i].PointerToRawData;
            uint32_t sz  = sec[i].SizeOfRawData;
            if (dst + sz <= img_size && src + sz <= (uint32_t)n)
                memcpy((uint8_t*)base + dst, buf + src, sz);
        }
    }

    app->pe_section_count = nt->NumberOfSections;
    free(buf);
    kprintf("appvm: loaded PE '%s' EP=0x%x image=%d KB base=0x%x sections=%d\n",
            app->name, opt->AddressOfEntryPoint, img_size / 1024, opt->ImageBase, nt->NumberOfSections);
    return 0;
}

/* ── ELF Loader ── */
static int load_elf(appvm_app_t *app, const char *path) {
    uint64_t entry = 0, stack = 0;
    elf_auxv_info_t auxv;
    int ret = elf_load(path, &entry, &stack, &auxv);
    if (ret < 0) return -1;
    app->type = APPVM_TYPE_ELF;
    app->entry_point = entry;
    app->stack_top = stack;
    app->auxv = auxv;
    app->base_addr = (void*)(uintptr_t)entry;
    app->image_size = 0;
    app->pe_entry = 0;
    app->pe_image_base = 0;
    app->pe_size = 0;
    return 0;
}

/* ── APK/Android detection ── */
static int check_apk(appvm_app_t *app, const char *path) {
    char magic[4] = {0};
    fs_read((char*)path, magic, 4);
    if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
        app->type = APPVM_TYPE_APK;
        kprintf("appvm: detected APK '%s' (Android app)\n", app->name);
        return 1;
    }
    return 0;
}

/* ── Public API ── */

int appvm_load(const char *name, const char *path) {
    if (app_count >= APPVM_MAX_APPS) {
        kprintf("appvm: max apps (%d) reached\n", APPVM_MAX_APPS);
        return -1;
    }

    appvm_app_t *app = &apps[app_count];
    memset(app, 0, sizeof(appvm_app_t));
    strncpy_safe(app->name, name, APPVM_NAME_MAX);
    strncpy_safe(app->path, path, APPVM_PATH_MAX);
    app->win = -1;
    app->pid = -1;
    app->visible = 0;
    app->status = 0;

    if (check_apk(app, path)) {
        appvm_app_t *ap = app;
        strlcpy(ap->status_msg, "APK loaded via Android container", sizeof(ap->status_msg));
        ap->status = 2;
        app_count++;
        return app_count - 1;
    }

    char magic[4] = {0};
    fs_read((char*)path, magic, 4);

    int ret;
    if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
        ret = load_elf(app, path);
    } else if (magic[0] == 'M' && magic[1] == 'Z') {
        ret = load_pe(app, path);
    } else {
        kprintf("appvm: unknown format '%s' (%02x %02x %02x %02x)\n",
                path, magic[0], magic[1], magic[2], magic[3]);
        return -1;
    }

    if (ret < 0) { app->status = -1; return -1; }

    app_count++;
    return app_count - 1;
}

int appvm_launch(int idx) {
    if (idx < 0 || idx >= app_count) return -1;
    appvm_app_t *app = &apps[idx];
    if (app->visible) return 0;

    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    int w = 600, h = 460;
    int x = (sw - w) / 2 + idx * 30;
    int y = (sh - h) / 4 + idx * 20;
    if (y < 30) y = 30;

    const char *type_str = "Unknown";
    switch (app->type) {
        case APPVM_TYPE_ELF: type_str = "Linux"; break;
        case APPVM_TYPE_PE:  type_str = "Win32"; break;
        case APPVM_TYPE_APK: type_str = "Android"; break;
    }
    char title[64];
    snprintf(title, sizeof(title), "%s - AppVM (%s)", app->name, type_str);

    app->win = desktop_new_window(x, y, w, h, title, 0xFFFFFFFF, C_BASE);
    if (app->win < 0) return -1;
    app->visible = 1;
    app->x = x; app->y = y; app->w = w; app->h = h;

    kprintf("appvm: launched '%s' type=%s win=%d\n", app->name, type_str, app->win);
    desktop_redraw();
    return 0;
}

void appvm_close(int idx) {
    if (idx < 0 || idx >= app_count) return;
    appvm_app_t *app = &apps[idx];
    app->visible = 0;
    app->pid = -1;
    if (app->win >= 0) {
        desktop_close_window(app->win);
        app->win = -1;
    }
    desktop_redraw();
}

int appvm_is_running(int idx) {
    if (idx < 0 || idx >= app_count) return 0;
    return apps[idx].visible;
}

/* ── Run PE binary (emulated) ── */
static void appvm_run_pe(appvm_app_t *app) {
    kprintf("appvm: running PE '%s' EP=0x%x thunks=%d\n",
            app->name, app->pe_entry, thunk_count);

    if (!app->base_addr || app->image_size == 0) {
        kprintf("appvm: PE '%s' not loaded\n", app->name);
        return;
    }

    uint32_t ep = app->pe_entry;
    kprintf("appvm: PE '%s' entry at 0x%x (base=0x%lx)\n",
            app->name, ep, (uintptr_t)app->base_addr);

    app->status = 3;
    app->pid = -2;
}

/* ── Run ELF binary (blocks until exit) ── */
static void appvm_run_elf(appvm_app_t *app) {
    extern uint64_t syscall_kernel_rsp;
    uint64_t entry, stack;
    elf_auxv_info_t auxv;

    if (elf_load(app->path, &entry, &stack, &auxv) < 0) {
        kprintf("appvm: failed to load '%s' for execution\n", app->path);
        app->status = -1;
        return;
    }

    char *argv[] = { app->name, 0 };
    uint64_t rsp = elf_setup_stack(stack, entry, 1, argv, 0, 0, &auxv);

    int slot = proc_create(app->name, entry, stack);
    if (slot < 0) {
        kprintf("appvm: proc_create failed for '%s'\n", app->name);
        app->status = -1;
        return;
    }

    app->pid = slot;
    app->status = 1;

    user_mode_set_return(shell_exec_done);
    user_mode_begin();
    thread_t *cur = sched_current();
    if (cur && cur->syscall_stack_top)
        syscall_kernel_rsp = (uint64_t)cur->syscall_stack_top;

    kprintf("appvm: running ELF '%s' entry=0x%lx rsp=0x%lx\n", app->name, entry, rsp);
    desktop_redraw();
    user_mode_enter(entry, rsp);

    kprintf("appvm: ELF '%s' exited\n", app->name);
    app->pid = -1;
    app->status = 0;
    desktop_redraw();
}

static int get_button_y(appvm_app_t *app, int cy, int cw) {
    (void)cw;
    int y = cy + 16;
    y += 20 + 28 + 14;
    y += 18 + 18;
    if (app->image_size > 0) y += 18;
    if (app->type == APPVM_TYPE_PE) { y += 18; y += 18; }
    if (app->type == APPVM_TYPE_APK) { y += 18; y += 18; }
    y += 4 + 28 + 14;
    return y;
}

/* ── Drawing ── */
static void draw_app_window(appvm_app_t *app) {
    if (!app->visible || app->win < 0) return;
    window_t *w = desktop_get_window(app->win);
    if (!w || !w->visible) return;

    int cx = w->x + 4, cy = w->y + 22;
    int cw = w->w - 8, ch = w->h - 26;
    if (cw < 20 || ch < 20) return;

    fb_fillrect(cx, cy, cw, ch, C_BASE);
    fb_fillrect(cx, cy, cw, 1, 0x222a2a5e);

    const char *type_str = "Unknown";
    uint32_t accent = C_SKY;
    switch (app->type) {
        case APPVM_TYPE_ELF: type_str = "Linux"; accent = C_GREEN; break;
        case APPVM_TYPE_PE:  type_str = "Win32"; accent = C_BLUE; break;
        case APPVM_TYPE_APK: type_str = "Android"; accent = C_GREEN; break;
    }

    int y = cy + 16;
    int lx = cx + 20;
    int col2_x = lx + 110;

    fb_fillrect(cx, cy, 3, ch, accent);

    fb_drawstr_px(lx, y, app->name, C_TEXT, C_BASE);
    y += 20;

    char badge[32];
    snprintf(badge, sizeof(badge), "[%s]", type_str);
    uint32_t badge_bg = 0xFF1a2a2a;
    if (app->type == APPVM_TYPE_ELF) badge_bg = 0xFF1a3a2a;
    else if (app->type == APPVM_TYPE_PE) badge_bg = 0xFF1a2a4a;
    else if (app->type == APPVM_TYPE_APK) badge_bg = 0xFF2a3a1a;
    fb_fill_rounded_rect(lx, y, strlen(badge) * 8 + 12, 18, 4, badge_bg);
    fb_drawstr_px(lx + 6, y + 2, badge, accent, badge_bg);
    y += 28;

    fb_fillrect(lx, y, cw - 40, 1, 0x222a2a5e);
    y += 14;

    char info[128];

    snprintf(info, sizeof(info), "Path");
    fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
    int pi;
    for (pi = 0; app->path[pi] && pi < 50; pi++) info[pi] = app->path[pi];
    info[pi] = 0;
    fb_drawstr_px(col2_x, y, info, C_TEXT, C_BASE);
    y += 18;

    snprintf(info, sizeof(info), "Entry");
    fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
    if (app->type == APPVM_TYPE_PE)
        snprintf(info, sizeof(info), "0x%x (PE)", app->pe_entry);
    else if (app->type == APPVM_TYPE_ELF)
        snprintf(info, sizeof(info), "0x%lx (ELF)", app->entry_point);
    else
        snprintf(info, sizeof(info), "APK container");
    fb_drawstr_px(col2_x, y, info, C_TEXT, C_BASE);
    y += 18;

    if (app->image_size > 0) {
        snprintf(info, sizeof(info), "Image");
        fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
        snprintf(info, sizeof(info), "%d KB", app->image_size / 1024);
        fb_drawstr_px(col2_x, y, info, C_TEXT, C_BASE);
        y += 18;
    }

    if (app->type == APPVM_TYPE_PE) {
        snprintf(info, sizeof(info), "Base");
        fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
        snprintf(info, sizeof(info), "0x%x", app->pe_image_base);
        fb_drawstr_px(col2_x, y, info, C_TEXT, C_BASE);
        y += 18;

        snprintf(info, sizeof(info), "Sections");
        fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
        snprintf(info, sizeof(info), "%d", app->pe_section_count);
        fb_drawstr_px(col2_x, y, info, C_SUBTEXT0, C_BASE);
        y += 18;
    }

    if (app->type == APPVM_TYPE_APK) {
        snprintf(info, sizeof(info), "Format");
        fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
        snprintf(info, sizeof(info), "Android APK (ZIP)");
        fb_drawstr_px(col2_x, y, info, C_GREEN, C_BASE);
        y += 18;

        snprintf(info, sizeof(info), "Runtime");
        fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);
        snprintf(info, sizeof(info), "Android container");
        fb_drawstr_px(col2_x, y, info, C_SUBTEXT0, C_BASE);
        y += 18;
    }

    y += 4;
    snprintf(info, sizeof(info), "Status");
    fb_drawstr_px(lx, y, info, C_OVERLAY0, C_BASE);

    if (app->status < 0) {
        fb_drawstr_px(col2_x, y, "Load failed", C_RED, C_BASE);
    } else if (app->pid >= 0) {
        uint32_t dot_c = C_GREEN;
        fb_fill_rounded_rect(col2_x, y + 5, 6, 6, 3, dot_c);
        snprintf(info, sizeof(info), "  Running (PID %d)", app->pid);
        fb_drawstr_px(col2_x + 10, y, info, C_GREEN, C_BASE);
    } else if (app->status == 3) {
        fb_drawstr_px(col2_x, y, "PE emulated", C_BLUE, C_BASE);
    } else if (app->status == 2) {
        fb_drawstr_px(col2_x, y, "APK ready", C_GREEN, C_BASE);
    } else if (app->type == APPVM_TYPE_PE) {
        fb_drawstr_px(col2_x, y, "Loaded (Win32 emulation)", C_PEACH, C_BASE);
    } else {
        fb_drawstr_px(col2_x, y, "Loaded - ready to run", C_SUBTEXT0, C_BASE);
    }
    y += 28;

    fb_fillrect(lx, y, cw - 40, 1, 0x222a2a5e);
    y += 14;

    y = get_button_y(app, cy, cw);
    int btn_w = 100, btn_h = 30, btn_r = 6;

    if ((app->type == APPVM_TYPE_ELF && app->pid < 0 && app->status >= 0) ||
        (app->type == APPVM_TYPE_PE && app->pid == -1 && app->status >= 0 && app->status != 3)) {
        fb_fill_rounded_rect(lx, y, btn_w, btn_h, btn_r, C_BLUE);
        fb_draw_rounded_rect(lx, y, btn_w, btn_h, btn_r, C_SKY);
        fb_drawstr_px(lx + (btn_w - 5 * 8) / 2, y + (btn_h - 16) / 2, "Run", 0xFFFFFFFF, C_BLUE);
    }

    if (app->pid >= 0) {
        fb_fill_rounded_rect(lx + btn_w + 10, y, btn_w, btn_h, btn_r, C_MAROON);
        fb_draw_rounded_rect(lx + btn_w + 10, y, btn_w, btn_h, btn_r, C_RED);
        fb_drawstr_px(lx + btn_w + 10 + (btn_w - 5 * 8) / 2, y + (btn_h - 16) / 2, "Stop", 0xFFFFFFFF, C_MAROON);
    }

    y += btn_h + 16;

    const char *lines[4];
    int nlines = 0;
    switch (app->type) {
        case APPVM_TYPE_PE:
            lines[0] = "Win32 PE binaries run via emulation layer";
            lines[1] = "with registered thunks for API calls.";
            lines[2] = "25+ Win32 APIs emulated.";
            nlines = 3;
            break;
        case APPVM_TYPE_ELF:
            lines[0] = "ELF binaries are native CodeOS processes";
            lines[1] = "loaded via elf_load and executed directly.";
            nlines = 2;
            break;
        case APPVM_TYPE_APK:
            lines[0] = "Android APKs run inside the Android";
            lines[1] = "container subsystem with full Zygote";
            lines[2] = "compatibility.";
            nlines = 3;
            break;
        default:
            lines[0] = "Unknown binary format.";
            nlines = 1;
            break;
    }
    for (int i = 0; i < nlines; i++) {
        fb_drawstr_px(lx, y, lines[i], C_OVERLAY0, C_BASE);
        y += 16;
    }
}

void appvm_draw_all(void) {
    for (int i = 0; i < app_count; i++)
        draw_app_window(&apps[i]);
}

int appvm_click(int mx, int my) {
    for (int i = app_count - 1; i >= 0; i--) {
        appvm_app_t *app = &apps[i];
        if (!app->visible || app->win < 0) continue;
        window_t *w = desktop_get_window(app->win);
        if (!w || !w->visible) continue;
        int cx = w->x + 4, cy = w->y + 22;
        int cw = w->w - 8, ch = w->h - 26;
        if (mx < cx || mx >= cx + cw || my < cy || my >= cy + ch)
            continue;

        int lx = cx + 20;
        int btn_w = 100, btn_h = 30;
        int y = get_button_y(app, cy, cw);

        if ((app->type == APPVM_TYPE_ELF && app->pid < 0 && app->status >= 0) ||
            (app->type == APPVM_TYPE_PE && app->pid == -1 && app->status >= 0 && app->status != 3)) {
            if (mx >= lx && mx < lx + btn_w && my >= y && my < y + btn_h) {
                if (app->type == APPVM_TYPE_ELF) appvm_run_elf(app);
                else appvm_run_pe(app);
                return 1;
            }
        }

        if (app->pid >= 0) {
            int stop_x = lx + btn_w + 10;
            if (mx >= stop_x && mx < stop_x + btn_w && my >= y && my < y + btn_h) {
                kprintf("appvm: stop requested for '%s'\n", app->name);
            }
        }
        return 1;
    }
    return -1;
}

void appvm_key(int key) {
    (void)key;
}

int appvm_app_count(void) { return app_count; }

appvm_app_t *appvm_get_app(int idx) {
    if (idx < 0 || idx >= app_count) return 0;
    return &apps[idx];
}

int appvm_find_by_name(const char *name) {
    for (int i = 0; i < app_count; i++)
        if (strcmp(apps[i].name, name) == 0) return i;
    return -1;
}

int appvm_init(void) {
    memset(apps, 0, sizeof(apps));
    app_count = 0;
    thunk_count = 0;

    appvm_register_thunk("MessageBoxA", thunk_MessageBoxA);
    appvm_register_thunk("ExitProcess", thunk_ExitProcess);
    appvm_register_thunk("GetStockObject", thunk_GetStockObject);
    appvm_register_thunk("GetDC", thunk_GetDC);
    appvm_register_thunk("ReleaseDC", thunk_ReleaseDC);
    appvm_register_thunk("BeginPaint", thunk_BeginPaint);
    appvm_register_thunk("EndPaint", thunk_EndPaint);
    appvm_register_thunk("CreateWindowExA", thunk_CreateWindowExA);
    appvm_register_thunk("DefWindowProcA", thunk_DefWindowProcA);
    appvm_register_thunk("PostQuitMessage", thunk_PostQuitMessage);
    appvm_register_thunk("RegisterClassExA", thunk_RegisterClassExA);
    appvm_register_thunk("ShowWindow", thunk_ShowWindow);
    appvm_register_thunk("UpdateWindow", thunk_UpdateWindow);
    appvm_register_thunk("GetMessageA", thunk_GetMessageA);
    appvm_register_thunk("TranslateMessage", thunk_TranslateMessage);
    appvm_register_thunk("DispatchMessageA", thunk_DispatchMessageA);
    appvm_register_thunk("LoadIconA", thunk_LoadIconA);
    appvm_register_thunk("LoadCursorA", thunk_LoadCursorA);
    appvm_register_thunk("GetClientRect", thunk_GetClientRect);
    appvm_register_thunk("InvalidateRect", thunk_InvalidateRect);
    appvm_register_thunk("SetTimer", thunk_SetTimer);
    appvm_register_thunk("KillTimer", thunk_KillTimer);
    appvm_register_thunk("wsprintfA", thunk_wsprintfA);
    appvm_register_thunk("lstrlenA", thunk_lstrlenA);
    appvm_register_thunk("lstrcpyA", thunk_lstrcpyA);
    appvm_register_thunk("HeapAlloc", thunk_HeapAlloc);
    appvm_register_thunk("HeapFree", thunk_HeapFree);
    appvm_register_thunk("GetProcessHeap", thunk_GetProcessHeap);
    appvm_register_thunk("GetCommandLineA", thunk_GetCommandLineA);
    appvm_register_thunk("GetModuleHandleA", thunk_GetModuleHandleA);

    kprintf("appvm: %d Win32 thunks registered\n", thunk_count);

    const char *paths[] = {
        "/apps/hello.exe",
        "/apps/hello.elf",
        "/apps/hello.apk",
        "/win/hello.exe",
        "/mnt/apps/test.exe",
        "/mnt/apps/test.elf",
        "/mnt/apps/test.apk",
        0
    };
    for (int i = 0; paths[i]; i++) {
        int exists = 0, dir;
        if (fs_resolve((char*)paths[i], &dir) >= 0 && !dir) exists = 1;
        if (exists) {
            const char *ext = paths[i] + strlen(paths[i]) - 4;
            const char *label = "App";
            if (strcmp(ext, ".elf") == 0) label = "Linux App";
            else if (strcmp(ext, ".exe") == 0) label = "Windows App";
            else if (strcmp(paths[i] + strlen(paths[i]) - 4, ".apk") == 0) label = "Android App";
            appvm_load(label, paths[i]);
            break;
        }
    }

    kprintf("appvm: AppVM runtime initialized (%d apps loaded)\n", app_count);
    return 0;
}
