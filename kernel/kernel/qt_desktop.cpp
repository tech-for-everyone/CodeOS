extern "C" {
#include "kprintf.h"
}

extern "C" int qt6_panels_init(void);
extern "C" void qt6_panels_run(void);
extern "C" void qt6_panels_stop(void);
extern "C" int qt6_panels_active(void);

extern "C" int qt_desktop_init(void) {
    extern char __tls_end[];
    *(uint64_t *)__tls_end = (uint64_t)__tls_end;
    kprintf("Qt6 Desktop: init\n");
    int ok = qt6_panels_init();
    if (ok)
        kprintf_disable_output();
    return ok;
}

extern "C" int qt_desktop_active(void) {
    return qt6_panels_active();
}

extern "C" void qt_desktop_run(void) {
    kprintf("Qt6 Desktop: run\n");
    qt6_panels_run();
}

extern "C" void qt_desktop_stop(void) {
    qt6_panels_stop();
}

/* Small ABI compatibility hook for the Zircon service.  Zircon is not the
 * desktop renderer; it only needs the reserved menu-bar offset when laying
 * out its own service surface.  Rendering and window ownership stay in Qt. */
extern "C" int panels_menubar_h(void) {
    return 28;
}

/* Legacy service entry points now route to the Qt desktop.  Keep the kernel
 * ABI linkable without bringing the former C panel implementation back into
 * the image. */
extern "C" int installer_open(void) { return -1; }
extern "C" int terminal_open(void) { return -1; }
extern "C" int fmanager_open(void) { return -1; }
extern "C" int settings_open(void) { return -1; }
extern "C" int about_open(void) { return -1; }
extern "C" void calc_open(void) {}
extern "C" int openweb_open(void) { return -1; }
extern "C" int appvm_init(void) { return 0; }
