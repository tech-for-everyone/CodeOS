#include "drivers.h"
#include "input.h"
#include "mouse.h"
#include "timer.h"
#include "speaker.h"
#include "ata.h"
#include "ahci.h"
#include "usb.h"
#include "nic.h"
#include "virtio_gpu.h"
#include "wifi.h"
#include "../kernel/kprintf.h"
#include "net.h"
#include "../kernel/string.h"
#include "../kernel/block.h"

#ifdef __aarch64__
#include "../arch/arm64/rtc.h"
#include "../arch/arm64/serial.h"
#include "../arch/arm64/fb.h"
#else
#include "../arch/x86_64/rtc.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/fb.h"
#endif

void drivers_init(void) {
    __asm__ volatile("cli");
    input_init();
    __asm__ volatile("sti");
    timer_init(1000);
    rtc_init();
#ifdef __aarch64__
    /* ARM64 virt has no PC speaker */
    (void)speaker_init;
#else
    speaker_init();
#endif
    usb_init();
    wifi_virtio_init();
}

void drivers_print_status(void) {
    kprintf("Drivers:\n");
    kprintf("  serial   COM1 (arch)\n");
    kprintf("  timer    %u Hz\n", timer_get_frequency());
    kprintf("  ps2      keyboard + %s\n", mouse_available() ? "mouse" : "no mouse");
    kprintf("  rtc      %s\n", rtc_available() ? "available" : "unavailable");
#ifdef __aarch64__
    kprintf("  speaker  none\n");
#else
    kprintf("  speaker  PC beeper\n");
#endif
    kprintf("  storage  %s\n", block_available() ? block_backend_name() : "none");
    kprintf("  nic      %s\n", net_ready() ? nic_name() : "down");
    kprintf("  wifi     %s\n", wifi_is_connected() ? "connected" : wifi_state_str(wifi_get_state()));
    kprintf("  usb      %s\n", usb_available() ? "controller(s) found" : "none");
    kprintf("  bluetooth %s\n", usb_bt_available() ? "detected" : "none");
    kprintf("  serial_acm %s\n", usb_acm_available() ? "detected" : "none");
    kprintf("  audio     %s\n", usb_audio_available() ? "detected" : "none");
    kprintf("  video     %s\n", usb_uvc_available() ? "detected" : "none");
    kprintf("  gamepad   %s\n", usb_gamepad_available() ? "detected" : "none");
    kprintf("  printer   %s\n", usb_printer_available() ? "detected" : "none");
    kprintf("  rndis     %s\n", usb_rndis_available() ? "detected" : "none");
    kprintf("  mtp       %s\n", usb_mtp_available() ? "detected" : "none");
    kprintf("  wacom     %s\n", usb_wacom_available() ? "detected" : "none");
    kprintf("  dfu       %s\n", usb_dfu_available() ? "detected" : "none");
}

void drivers_print_status_panel(void) {
    uint32_t dim_fg = FB_RGB(85,85,85), dim_bg = FB_RGB(0,0,0);
    int row = 17;

    fb_write_styled(row, 2, "  serial: COM1 (arch)", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  timer:  ", dim_fg, dim_bg);
    char fbuf[8];
    snprintf(fbuf, sizeof(fbuf), "%u", timer_get_frequency());
    fb_write_styled(row, 11, fbuf, dim_fg, dim_bg);
    fb_write_styled(row, 11 + (int)strlen(fbuf), " Hz", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  ps2:    keyboard + ", dim_fg, dim_bg);
    fb_write_styled(row, 22, mouse_available() ? "mouse" : "no mouse", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  rtc:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, rtc_available() ? "available" : "unavailable", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  strg:  ", dim_fg, dim_bg);
    fb_write_styled(row, 11, block_available() ? block_backend_name() : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  nic:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, net_ready() ? nic_name() : "down", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  usb:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_available() ? "controller(s) found" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  bt:    ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_bt_available() ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  acm:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_acm_available() ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  audio: ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_audio_available() ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  video: ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_uvc_available() ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  gp:    ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_gamepad_available() ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  print: ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_printer_available() ? "detected" : "none", dim_fg, dim_bg);
}
