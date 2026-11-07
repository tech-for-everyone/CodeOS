#include "drivers.h"
#include "input.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "speaker.h"
#include "ata.h"
#include "ahci.h"
#include "usb.h"
#include "usb_ehci.h"
#include "usb_uhci.h"
#include "usb_xhci.h"
#include "mouse_g305.h"
#include "kbd_aula_f87.h"
#include "nic.h"
#include "virtio_gpu.h"
#include "wifi.h"
#include "net.h"
#include "../kernel/kprintf.h"
#include "../kernel/string.h"
#include "../kernel/block.h"
#include "../kernel/slab.h"

#ifdef __aarch64__
#include "../arch/arm64/rtc.h"
#include "../arch/arm64/serial.h"
#include "../arch/arm64/fb.h"
#include "../arch/arm64/pci.h"
#else
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/rtc.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/fb.h"
#include "../arch/x86_64/pci.h"
#endif

/* ──────────────────────────────────────────────────────────────────────────
 *  Hardware Probes & Dynamic Initializers
 * ────────────────────────────────────────────────────────────────────────── */

/* Timer: PIT 8254 */
static int probe_timer(void) {
#ifdef __aarch64__
    return 1;
#else
    /* Non-destructive check of PIT control port 0x43 */
    outb(0x43, 0x00); /* latch counter 0 */
    uint8_t low = inb(0x40);
    uint8_t high = inb(0x40);
    return (low != 0xFF || high != 0xFF) ? 1 : 1;
#endif
}
static int load_timer(void *ram_inst) {
    (void)ram_inst;
    timer_init(1000);
    return 0;
}

/* RTC / CMOS Clock */
static int probe_rtc(void) {
    return rtc_available();
}
static int load_rtc(void *ram_inst) {
    (void)ram_inst;
    rtc_init();
    return 0;
}

/* PC Speaker */
static int probe_speaker(void) {
#ifdef __aarch64__
    return 0;
#else
    uint8_t v = inb(0x61);
    return (v != 0xFF);
#endif
}
static int load_speaker(void *ram_inst) {
    (void)ram_inst;
#ifndef __aarch64__
    speaker_init();
#endif
    return 0;
}

/* PS/2 Keyboard */
static int probe_ps2_kbd(void) {
#ifdef __aarch64__
    return 0;
#else
    uint8_t stat = inb(0x64);
    return (stat != 0xFF);
#endif
}
static int load_ps2_kbd(void *ram_inst) {
    (void)ram_inst;
    keyboard_init();
    return 0;
}

/* PS/2 Mouse */
static int probe_ps2_mouse(void) {
#ifdef __aarch64__
    return 0;
#else
    uint8_t stat = inb(0x64);
    if (stat == 0xFF) return 0;
    return mouse_available();
#endif
}
static int load_ps2_mouse(void *ram_inst) {
    (void)ram_inst;
    mouse_init();
    return 0;
}

/* Logitech G305 Lightspeed Mouse */
static int probe_g305(void) {
    return g305_probe();
}
static int load_g305(void *ram_inst) {
    return g305_init(ram_inst);
}

/* AULA F87 Mechanical Keyboard */
static int probe_aula_f87(void) {
    return aula_f87_probe();
}
static int load_aula_f87(void *ram_inst) {
    return aula_f87_init(ram_inst);
}

/* UHCI USB 1.1 Host Controller */
static int probe_uhci(void) {
    uint8_t bus, slot, func;
    return pci_find_class(0x0C, 0x03, &bus, &slot, &func);
}
static int load_uhci(void *ram_inst) {
    (void)ram_inst;
    return uhci_init();
}

/* EHCI USB 2.0 Host Controller */
static int probe_ehci(void) {
    int dev_idx = 0;
    uint8_t bus, slot, func;
    while (pci_find_class_idx(0x0C, 0x03, dev_idx, &bus, &slot, &func)) {
        uint32_t rev = pci_config_read(bus, slot, func, 0x08);
        if (((rev >> 8) & 0xFF) == 0x20) return 1;
        dev_idx++;
    }
    return 0;
}
static int load_ehci(void *ram_inst) {
    (void)ram_inst;
    return ehci_init();
}

/* xHCI USB 3.0 Host Controller */
static int probe_xhci(void) {
    uint8_t bus, slot, func;
    if (!pci_find_class_idx(0x0C, 0x03, 0, &bus, &slot, &func)) return 0;
    uint8_t prog_if = (pci_config_read(bus, slot, func, 0x08) >> 8) & 0xFF;
    return (prog_if == 0x30);
}
static int load_xhci(void *ram_inst) {
    (void)ram_inst;
    return xhci_init();
}

/* Generic USB HID Relative Mouse */
static int probe_usb_mouse(void) {
    return usb_mouse_available();
}
static int load_usb_mouse(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* Generic USB HID Boot Keyboard */
static int probe_usb_kbd(void) {
    return usb_kbd_available();
}
static int load_usb_kbd(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* USB Touchscreen / Absolute Digitizer */
static int probe_usb_touch(void) {
    return usb_touch_available();
}
static int load_usb_touch(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* USB Bluetooth HCI */
static int probe_usb_bt(void) {
    return usb_bt_available();
}
static int load_usb_bt(void *ram_inst) {
    (void)ram_inst;
    return usb_bt_init();
}

/* USB CDC ACM Serial */
static int probe_usb_acm(void) {
    return usb_acm_available();
}
static int load_usb_acm(void *ram_inst) {
    (void)ram_inst;
    return usb_acm_init();
}

/* USB Audio */
static int probe_usb_audio(void) {
    return usb_audio_available();
}
static int load_usb_audio(void *ram_inst) {
    (void)ram_inst;
    return usb_audio_init();
}

/* USB UVC Webcam */
static int probe_usb_uvc(void) {
    return usb_uvc_available();
}
static int load_usb_uvc(void *ram_inst) {
    (void)ram_inst;
    return usb_uvc_init();
}

/* USB Gamepad */
static int probe_usb_gamepad(void) {
    return usb_gamepad_available();
}
static int load_usb_gamepad(void *ram_inst) {
    (void)ram_inst;
    return usb_gamepad_init();
}

/* USB Printer */
static int probe_usb_printer(void) {
    return usb_printer_available();
}
static int load_usb_printer(void *ram_inst) {
    (void)ram_inst;
    return usb_printer_init();
}

/* USB Mass Storage */
static int probe_usb_msc(void) {
    return usb_msc_available();
}
static int load_usb_msc(void *ram_inst) {
    (void)ram_inst;
    return usb_msc_init();
}

/* USB RNDIS */
static int probe_usb_rndis(void) {
    return usb_rndis_available();
}
static int load_usb_rndis(void *ram_inst) {
    (void)ram_inst;
    return usb_rndis_init();
}

/* USB MTP */
static int probe_usb_mtp(void) {
    return usb_mtp_available();
}
static int load_usb_mtp(void *ram_inst) {
    (void)ram_inst;
    return usb_mtp_init();
}

/* USB Wacom */
static int probe_usb_wacom(void) {
    return usb_wacom_available();
}
static int load_usb_wacom(void *ram_inst) {
    (void)ram_inst;
    return usb_wacom_init();
}

/* USB DFU */
static int probe_usb_dfu(void) {
    return usb_dfu_available();
}
static int load_usb_dfu(void *ram_inst) {
    (void)ram_inst;
    return usb_dfu_init();
}

/* VirtIO WiFi Wireless NIC */
static int probe_virtio_wifi(void) {
    uint8_t b, s, f;
    return pci_find_device(0x1AF4, 0x1000, &b, &s, &f) ||
           pci_find_device(0x1AF4, 0x1041, &b, &s, &f);
}
static int load_virtio_wifi(void *ram_inst) {
    (void)ram_inst;
    wifi_virtio_init();
    return 0;
}

/* Intel E1000 Ethernet NIC */
static int probe_e1000(void) {
    uint8_t b, s, f;
    return pci_find_device(0x8086, 0x100E, &b, &s, &f) ||
           pci_find_device(0x8086, 0x100F, &b, &s, &f);
}
static int load_e1000(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* AHCI SATA Storage */
static int probe_ahci(void) {
    uint8_t b, s, f;
    return pci_find_class(0x01, 0x06, &b, &s, &f);
}
static int load_ahci(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* ATA IDE Storage */
static int probe_ata(void) {
    uint8_t b, s, f;
    return pci_find_class(0x01, 0x01, &b, &s, &f);
}
static int load_ata(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* NVMe PCI-e SSD Storage */
static int probe_nvme(void) {
    uint8_t b, s, f;
    return pci_find_class(0x01, 0x08, &b, &s, &f);
}
static int load_nvme(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* AC97 Audio */
static int probe_ac97(void) {
    uint8_t b, s, f;
    return pci_find_class(0x04, 0x01, &b, &s, &f);
}
static int load_ac97(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* VirtIO GPU 2D/3D */
static int probe_virtio_gpu(void) {
    uint8_t b, s, f;
    return pci_find_device(0x1AF4, 0x1050, &b, &s, &f);
}
static int load_virtio_gpu(void *ram_inst) {
    (void)ram_inst;
    return 0;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Driver Module Registry
 * ────────────────────────────────────────────────────────────────────────── */

static driver_module_t driver_registry[] = {
    { "pit_timer",          "Intel 8254 Programmable Interval Timer",    DRIVER_CLASS_TIMER,      probe_timer,       load_timer,       NULL, 64,                         NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "rtc_cmos",           "Motorola 146818 Real-Time Clock & CMOS",    DRIVER_CLASS_SYSTEM,     probe_rtc,         load_rtc,         NULL, 64,                         NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "pc_speaker",         "PC Beeper Sound Generator (Port 0x61)",     DRIVER_CLASS_AUDIO,      probe_speaker,     load_speaker,     NULL, 32,                         NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "ps2_keyboard",       "PS/2 Intel 8042 Keyboard Controller",       DRIVER_CLASS_KEYBOARD,   probe_ps2_kbd,     load_ps2_kbd,     NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "ps2_mouse",          "PS/2 Intel 8042 Auxiliary Mouse Device",    DRIVER_CLASS_MOUSE,      probe_ps2_mouse,   load_ps2_mouse,   NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "logitech_g305_mouse","Logitech G305 Lightspeed Wireless Gaming",  DRIVER_CLASS_MOUSE,      probe_g305,        load_g305,        NULL, sizeof(struct g305_state),  NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "aula_f87_keyboard",  "AULA F87 Mechanical Keyboard (TKL/NKRO)",   DRIVER_CLASS_KEYBOARD,   probe_aula_f87,    load_aula_f87,    NULL, sizeof(struct aula_f87_state), NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_uhci",           "Intel Universal Host Controller (USB 1.1)", DRIVER_CLASS_BUS,        probe_uhci,        load_uhci,        NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_ehci",           "Enhanced Host Controller Interface (USB 2)",DRIVER_CLASS_BUS,        probe_ehci,        load_ehci,        NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_xhci",           "Extensible Host Controller (USB 3.0)",      DRIVER_CLASS_BUS,        probe_xhci,        load_xhci,        NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_hid_mouse",      "Generic USB HID Relative Mouse Driver",     DRIVER_CLASS_MOUSE,      probe_usb_mouse,   load_usb_mouse,   NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_hid_keyboard",   "Generic USB HID Boot/NKRO Keyboard",        DRIVER_CLASS_KEYBOARD,   probe_usb_kbd,     load_usb_kbd,     NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_touch",          "Generic USB HID Absolute Digitizer/Touch",  DRIVER_CLASS_INPUT,      probe_usb_touch,   load_usb_touch,   NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_bluetooth",      "USB Bluetooth Wireless Host Controller",    DRIVER_CLASS_PERIPHERAL, probe_usb_bt,      load_usb_bt,      NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_serial_acm",     "USB CDC-ACM Communications Port",           DRIVER_CLASS_PERIPHERAL, probe_usb_acm,     load_usb_acm,     NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_audio",          "USB Audio Streaming Device",                DRIVER_CLASS_AUDIO,      probe_usb_audio,   load_usb_audio,   NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_uvc",            "USB Video Class (UVC) Webcam Capture",      DRIVER_CLASS_VIDEO,      probe_usb_uvc,     load_usb_uvc,     NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_gamepad",        "USB HID Gamepad / Controller",              DRIVER_CLASS_INPUT,      probe_usb_gamepad, load_usb_gamepad, NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_printer",        "USB Class 0x07 Printer Interface",          DRIVER_CLASS_PERIPHERAL, probe_usb_printer, load_usb_printer, NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_storage",        "USB Mass Storage Bulk-Only Transport",      DRIVER_CLASS_STORAGE,    probe_usb_msc,     load_usb_msc,     NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_rndis",          "USB Remote NDIS Ethernet Adapter",          DRIVER_CLASS_NETWORK,    probe_usb_rndis,   load_usb_rndis,   NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_mtp",            "USB Media Transfer Protocol Interface",     DRIVER_CLASS_PERIPHERAL, probe_usb_mtp,     load_usb_mtp,     NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_wacom",          "USB Wacom Graphics Tablet / Digitizer",     DRIVER_CLASS_INPUT,      probe_usb_wacom,   load_usb_wacom,   NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "usb_dfu",            "USB Device Firmware Upgrade Interface",     DRIVER_CLASS_SYSTEM,     probe_usb_dfu,     load_usb_dfu,     NULL, 128,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "virtio_wifi",        "VirtIO Wireless Network Interface",         DRIVER_CLASS_NETWORK,    probe_virtio_wifi, load_virtio_wifi, NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "e1000_nic",          "Intel 82540EM Gigabit Ethernet NIC",        DRIVER_CLASS_NETWORK,    probe_e1000,       load_e1000,       NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "ahci_sata",          "Serial ATA Advanced Host Controller",       DRIVER_CLASS_STORAGE,    probe_ahci,        load_ahci,        NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "ata_ide",            "Parallel ATA IDE Storage Controller",       DRIVER_CLASS_STORAGE,    probe_ata,         load_ata,         NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "nvme_ssd",           "NVM Express High-Speed PCIe Storage",       DRIVER_CLASS_STORAGE,    probe_nvme,        load_nvme,        NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "ac97_audio",         "Intel AC'97 Audio Controller",              DRIVER_CLASS_AUDIO,      probe_ac97,        load_ac97,        NULL, 256,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
    { "virtio_gpu",         "VirtIO 2D/3D Graphics Acceleration",        DRIVER_CLASS_VIDEO,      probe_virtio_gpu,  load_virtio_gpu,  NULL, 512,                        NULL, DRIVER_STATUS_UNLOADED, 0 },
};

#define DRIVER_COUNT ((int)(sizeof(driver_registry) / sizeof(driver_registry[0])))

int driver_module_count(void) {
    return DRIVER_COUNT;
}

driver_module_t *driver_get_module_by_idx(int idx) {
    if (idx < 0 || idx >= DRIVER_COUNT) return NULL;
    return &driver_registry[idx];
}

driver_module_t *driver_find_module(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < DRIVER_COUNT; i++) {
        if (strcmp(driver_registry[i].name, name) == 0)
            return &driver_registry[i];
    }
    return NULL;
}

int driver_is_loaded(const char *name) {
    driver_module_t *m = driver_find_module(name);
    return (m && m->status == DRIVER_STATUS_LOADED);
}

void *driver_get_ram_address(const char *name) {
    driver_module_t *m = driver_find_module(name);
    return m ? m->ram_instance : NULL;
}

/* ──────────────────────────────────────────────────────────────────────────
 *  Automated Driver Detection Subsystem Core
 * ────────────────────────────────────────────────────────────────────────── */

void drivers_detect_and_load(void) {
    kprintf("====================================================================\n");
    kprintf(" [driver-detect] Initializing Automated Hardware Detection Subsystem\n");
    kprintf("====================================================================\n");

    /* Bootstrap generic USB subsystem so buses are populated for discovery */
    usb_init();

    int detected_count = 0;
    int skipped_count = 0;

    for (int i = 0; i < DRIVER_COUNT; i++) {
        driver_module_t *mod = &driver_registry[i];

        /* Execute runtime hardware probe */
        int present = 0;
        if (mod->probe) {
            present = mod->probe();
        }

        if (present) {
            /* Hardware was identified: dynamically allocate module state in volatile RAM */
            uint32_t alloc_sz = mod->ram_size ? mod->ram_size : 64;
            void *ram_ptr = kmalloc(alloc_sz);
            if (!ram_ptr) {
                kprintf("[driver-detect] ERROR: RAM allocation failed for driver '%s' (%u bytes)\n",
                        mod->name, alloc_sz);
                mod->status = DRIVER_STATUS_FAILED;
                continue;
            }
            memset(ram_ptr, 0, alloc_sz);
            mod->ram_instance = ram_ptr;

            /* Load and initialize driver into volatile memory */
            int r = 0;
            if (mod->load) {
                r = mod->load(mod->ram_instance);
            }

            if (r == 0) {
                mod->status = DRIVER_STATUS_LOADED;
                mod->load_time_ms = timer_get_milliseconds();
                detected_count++;
                kprintf("[driver-detect] DETECTED: %-18s -> LOADED into RAM [addr=0x%lx size=%u]\n",
                        mod->name, (unsigned long)(uintptr_t)mod->ram_instance, alloc_sz);
            } else {
                mod->status = DRIVER_STATUS_FAILED;
                kfree(mod->ram_instance);
                mod->ram_instance = NULL;
                kprintf("[driver-detect] FAILED:   %-18s -> load returned error %d\n", mod->name, r);
            }
        } else {
            /* Strict Policy: Drivers that are not detected by this subsystem will not be loaded under any circumstances. */
            mod->status = DRIVER_STATUS_NOT_PRESENT;
            mod->ram_instance = NULL;
            skipped_count++;
            kprintf("[driver-detect] SKIPPED:  %-18s (hardware not detected; driver module NOT loaded)\n",
                    mod->name);
        }
    }

    kprintf("[driver-detect] Detection complete: %d loaded into RAM, %d un-detected drivers withheld\n",
            detected_count, skipped_count);
    kprintf("====================================================================\n");
}

void drivers_init(void) {
    __asm__ volatile("cli");
    input_init();
    __asm__ volatile("sti");

    /* Run the automated detection subsystem and dynamically load matching drivers */
    drivers_detect_and_load();
}

void drivers_dump_detection_table(void) {
    kprintf("Automated Driver Detection & Dynamic RAM Allocation Table:\n");
    kprintf("%-4s %-20s %-12s %-18s %s\n", "Idx", "Driver Name", "Status", "RAM Location", "Description");
    kprintf("--------------------------------------------------------------------------------\n");
    for (int i = 0; i < DRIVER_COUNT; i++) {
        driver_module_t *m = &driver_registry[i];
        const char *st_str = "UNLOADED";
        if (m->status == DRIVER_STATUS_LOADED) st_str = "LOADED";
        else if (m->status == DRIVER_STATUS_NOT_PRESENT) st_str = "NOT PRESENT";
        else if (m->status == DRIVER_STATUS_FAILED) st_str = "FAILED";

        char addr_buf[24];
        if (m->ram_instance) {
            snprintf(addr_buf, sizeof(addr_buf), "0x%lx (%uB)",
                     (unsigned long)(uintptr_t)m->ram_instance, m->ram_size);
        } else {
            snprintf(addr_buf, sizeof(addr_buf), "none (unloaded)");
        }

        kprintf("[%02d] %-20s %-12s %-18s %s\n",
                i, m->name, st_str, addr_buf, m->description);
    }
}

void drivers_print_status(void) {
    kprintf("Drivers Subsystem Status:\n");
    kprintf("  serial    COM1 (arch)\n");
    kprintf("  timer     %u Hz (%s)\n", timer_get_frequency(),
            driver_is_loaded("pit_timer") ? "loaded in RAM" : "unloaded");
    kprintf("  ps2       keyboard (%s) + mouse (%s)\n",
            driver_is_loaded("ps2_keyboard") ? "loaded in RAM" : "unloaded",
            driver_is_loaded("ps2_mouse") ? "loaded in RAM" : "unloaded");
    kprintf("  g305      %s\n", g305_is_available() ? "Logitech G305 Lightspeed [ACTIVE in RAM]" : "not detected");
    kprintf("  aula_f87  %s\n", aula_f87_is_available() ? "AULA F87 Mechanical [ACTIVE in RAM]" : "not detected");
    kprintf("  rtc       %s\n", rtc_available() ? "available" : "unavailable");
#ifdef __aarch64__
    kprintf("  speaker   none\n");
#else
    kprintf("  speaker   %s\n", driver_is_loaded("pc_speaker") ? "PC beeper (active)" : "none");
#endif
    kprintf("  storage   %s\n", block_available() ? block_backend_name() : "none");
    kprintf("  nic       %s\n", net_ready() ? nic_name() : "down");
    kprintf("  wifi      %s\n", wifi_is_connected() ? "connected" : wifi_state_str(wifi_get_state()));
    kprintf("  usb       %s\n", usb_available() ? "controller(s) found" : "none");
    kprintf("  bluetooth %s\n", driver_is_loaded("usb_bluetooth") ? "detected [loaded in RAM]" : "none");
    kprintf("  serial_acm %s\n", driver_is_loaded("usb_serial_acm") ? "detected [loaded in RAM]" : "none");
    kprintf("  audio     %s\n", driver_is_loaded("usb_audio") ? "detected [loaded in RAM]" : "none");
    kprintf("  video     %s\n", driver_is_loaded("usb_uvc") ? "detected [loaded in RAM]" : "none");
    kprintf("  gamepad   %s\n", driver_is_loaded("usb_gamepad") ? "detected [loaded in RAM]" : "none");
    kprintf("  printer   %s\n", driver_is_loaded("usb_printer") ? "detected [loaded in RAM]" : "none");
    kprintf("  rndis     %s\n", driver_is_loaded("usb_rndis") ? "detected [loaded in RAM]" : "none");
    kprintf("  mtp       %s\n", driver_is_loaded("usb_mtp") ? "detected [loaded in RAM]" : "none");
    kprintf("  wacom     %s\n", driver_is_loaded("usb_wacom") ? "detected [loaded in RAM]" : "none");
    kprintf("  dfu       %s\n", driver_is_loaded("usb_dfu") ? "detected [loaded in RAM]" : "none");
}

void drivers_print_status_panel(void) {
    uint32_t dim_fg = FB_RGB(85,85,85), dim_bg = FB_RGB(0,0,0);
    uint32_t lit_fg = FB_RGB(0,255,128);
    int row = 17;

    fb_write_styled(row, 2, "  serial: COM1 (arch)", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  timer:  ", dim_fg, dim_bg);
    char fbuf[8];
    snprintf(fbuf, sizeof(fbuf), "%u", timer_get_frequency());
    fb_write_styled(row, 11, fbuf, dim_fg, dim_bg);
    fb_write_styled(row, 11 + (int)strlen(fbuf), " Hz", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  ps2:    keyboard + ", dim_fg, dim_bg);
    fb_write_styled(row, 22, mouse_available() ? "mouse" : "no mouse", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  g305:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, g305_is_available() ? "Logitech G305 Lightspeed [RAM]" : "not present",
                    g305_is_available() ? lit_fg : dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  f87:    ", dim_fg, dim_bg);
    fb_write_styled(row, 11, aula_f87_is_available() ? "AULA F87 Mechanical [RAM]" : "not present",
                    aula_f87_is_available() ? lit_fg : dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  rtc:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, rtc_available() ? "available" : "unavailable", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  strg:  ", dim_fg, dim_bg);
    fb_write_styled(row, 11, block_available() ? block_backend_name() : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  nic:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, net_ready() ? nic_name() : "down", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  usb:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, usb_available() ? "controller(s) found" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  bt:    ", dim_fg, dim_bg);
    fb_write_styled(row, 11, driver_is_loaded("usb_bluetooth") ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  acm:   ", dim_fg, dim_bg);
    fb_write_styled(row, 11, driver_is_loaded("usb_serial_acm") ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  audio: ", dim_fg, dim_bg);
    fb_write_styled(row, 11, driver_is_loaded("usb_audio") ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  video: ", dim_fg, dim_bg);
    fb_write_styled(row, 11, driver_is_loaded("usb_uvc") ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  gp:    ", dim_fg, dim_bg);
    fb_write_styled(row, 11, driver_is_loaded("usb_gamepad") ? "detected" : "none", dim_fg, dim_bg); row++;
    fb_write_styled(row, 2, "  print: ", dim_fg, dim_bg);
    fb_write_styled(row, 11, driver_is_loaded("usb_printer") ? "detected" : "none", dim_fg, dim_bg); row++;
}
