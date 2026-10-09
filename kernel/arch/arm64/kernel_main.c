/* Minimal ARM64 kernel_main.
 *
 * Standalone entry point that avoids x86_64-specific dependencies.  Brings up
 * serial, RTC, PCI, fw_cfg and the ramfb display, then hands control to the
 * interactive Zircon mobile UI (touch/pointer + keyboard driven). */
#include "serial.h"
#include "io.h"
#include "rtc.h"
#include "pci.h"
#include "fw_cfg.h"
#include "display.h"
#include "fb.h"
#include "input.h"
#include "ui.h"
#include "../kernel/types.h"
#include "../kernel/kprintf.h"

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

void kernel_main(uint32_t magic __attribute__((unused)),
                 void *mb_info __attribute__((unused))) {
    serial_init();
    kprintf_set_serial(serial_putchar);

    kprintf("\n");
    kprintf("==================================\n");
    kprintf("  Zircon 1.0 - ARM64 Mobile OS\n");
    kprintf("  QEMU virt (aarch64)\n");
    kprintf("==================================\n");
    kprintf("\n");

    kprintf("Kernel image: 0x%016lx - 0x%016lx (%lu KB)\n",
            (uint64_t)_kernel_start, (uint64_t)_kernel_end,
            ((uint64_t)_kernel_end - (uint64_t)_kernel_start) / 1024);

    rtc_init();
    kprintf("RTC: initialized (wall clock = %lu)\n",
            (uint64_t)rtc_read_wallclock());

    fw_cfg_init();

    kprintf("PCI: initializing ECAM...\n");
    pci_init();
    pci_scan();
    kprintf("PCI: %d devices found\n", pci_device_count());
    pci_print_devices();

    if (!display_init()) {
        kprintf("display: no ramfb device; idling\n");
        for (;;) { }
    }
    kprintf("display: %lux%lu framebuffer ready\n",
            (uint64_t)fb_getwidth(), (uint64_t)fb_getheight());

    input_init();
    ui_init();

    kprintf("ui: entering event loop\n");
    ui_run();

    for (;;) { }   /* not reached */
}