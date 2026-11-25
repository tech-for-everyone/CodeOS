/* CodeOS ARM64 kernel_main.
 *
 * This is the CodeOS ARM64 path (the default for ARCH=arm64).  It brings up
 * the portable ARM64 HAL -- PL011 serial, generic timer, fw_cfg, PCIe ECAM,
 * ramfb framebuffer and polling virtio/serial input -- and then hands control
 * to the CodeOS ARM64 shell/desktop.
 *
 * It deliberately does NOT launch the Zircon mobile UI.  That image is still
 * available with ARM64_UI=zircon (or `run.sh -a arm64 -z`) for the phone demo.
 */
#include "serial.h"
#include "io.h"
#include "rtc.h"
#include "pci.h"
#include "fw_cfg.h"
#include "display.h"
#include "fb.h"
#include "input.h"
#include "codeos_ui.h"
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
    kprintf("  CodeOS 1.0 - ARM64\n");
    kprintf("  QEMU virt (aarch64) - early port\n");
    kprintf("==================================\n\n");

    kprintf("Kernel image: 0x%016lx - 0x%016lx (%lu KB)\n",
            (uint64_t)_kernel_start, (uint64_t)_kernel_end,
            ((uint64_t)_kernel_end - (uint64_t)_kernel_start) / 1024);

    rtc_init();
    kprintf("timer: ARM generic timer ready (wall clock = %lu)\n",
            (uint64_t)rtc_read_wallclock());

    fw_cfg_init();

    pci_init();
    pci_scan();
    kprintf("pci: %d device(s) found\n", pci_device_count());

    if (!display_init()) {
        kprintf("display: no ramfb device; serial shell only\n");
        codeos_ui_run();
    }
    kprintf("display: %lux%lu framebuffer ready\n",
            (uint64_t)fb_getwidth(), (uint64_t)fb_getheight());

    input_init();

    kprintf("ui: entering CodeOS ARM64 shell\n");
    codeos_ui_run();
}
