#include <stddef.h>
#include <stdint.h>

#ifdef __aarch64__
#include "../arch/arm64/fb.h"
#include "../arch/arm64/serial.h"
#include "../arch/arm64/pci.h"
#include "../arch/arm64/io.h"
#include "../arch/arm64/rtc.h"
#else
#include "../arch/x86_64/multiboot.h"
#include "../arch/x86_64/fb.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/pci.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/rtc.h"
#include "../arch/x86_64/apic.h"
#endif

#include "../drivers/input.h"
#include "../drivers/mouse.h"
#include "../drivers/timer.h"
#include "../drivers/drivers.h"
#include "../drivers/hdmi.h"
#include "../drivers/gpu.h"
#include "../drivers/audio.h"
#include "../drivers/speaker.h"
#include "../drivers/usb.h"
#include "../drivers/wifi.h"
#include "../drivers/nic.h"

#include "mm.h"
#include "pmm.h"
#include "vmm.h"
#include "slab.h"
#include "idt.h"
#include "gdt.h"
#include "sched.h"
#include "process.h"
#include "syscall.h"
#include "rng.h"
#include "kprintf.h"
#include "string.h"
#include "version.h"
#include "bootsplash.h"
#include "fs.h"
#include "vfs.h"
#include "ext2.h"
#include "codefs.h"
#include "part.h"
#include "block.h"
#include "elf.h"
#include "umode.h"
#include "script.h"
#include "csl.h"
#include "shell.h"
#include "net.h"
#include "netdev.h"
#include "ethernet.h"
#include "arp.h"
#include "ip.h"
#include "udp.h"
#include "icmp.h"
#include "tcp.h"
#include "dns.h"
#include "socket.h"
#include "netstat.h"
#include "security.h"
#include "clamav.h"
#include "ad_block.h"
#include "desktop.h"
#include "qt_desktop.h"
#include "pkg.h"
#include "android.h"
#include "android_display.h"
#include "android_prop.h"
#include "android_rc.h"
#include "android_container.h"
#include "container.h"
#include "appvm.h"
#include "vm_manager.h"
#include "adb.h"
#include "updater.h"
#include "ipc.h"
#include "audit.h"
#include "inotify.h"
#include "namespace.h"
#include "rootfs.h"
#include "zircon.h"
#include "display_bridge.h"
#include "audio_mixer.h"
#include "x11_server.h"

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

/* Boot mode flags — set from Limine command line */
int kernel_recovery_mode = 0;
int kernel_safe_mode = 0;

typedef void (*ctor_fn_t)(void);
extern ctor_fn_t __preinit_array_start[];
extern ctor_fn_t __preinit_array_end[];
extern ctor_fn_t __init_array_start[];
extern ctor_fn_t __init_array_end[];

extern void fb_putchar(char);
extern void fb_clear(void);
#ifndef __aarch64__
extern void syscall_entry(void);
extern uint64_t syscall_kernel_rsp;
#endif

extern void initramfs_populate(void);
extern int rootfs_seed_android_stock(void);
extern void fpu_init_early(void);
extern int  limine_booted(void);
extern uint64_t limine_hhdm_offset(void);
extern int  limine_setup_fb(void);
extern void limine_setup_mm(uint64_t *mem_size);
extern const char *limine_get_cmdline(void);

extern int  net_init(void);
extern void shell_selftest(void);
extern void netstat_dump(void);
extern void slab_dump_stats(void);

#ifndef __aarch64__
#define VBE_DISPI_INDEX       0x1CE
#define VBE_DISPI_DATA        0x1CF
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_INDEX_XRES  1
#define VBE_DISPI_INDEX_YRES  2
#define VBE_DISPI_INDEX_BPP   3

static void vbe_write(uint16_t idx, uint16_t val) {
    outw(VBE_DISPI_INDEX, idx);
    outw(VBE_DISPI_DATA, val);
}

static void vbe_set_mode(uint16_t x, uint16_t y, uint16_t bpp) {
    vbe_write(VBE_DISPI_INDEX_ENABLE, 0);
    vbe_write(VBE_DISPI_INDEX_XRES, x);
    vbe_write(VBE_DISPI_INDEX_YRES, y);
    vbe_write(VBE_DISPI_INDEX_BPP, bpp);
    vbe_write(VBE_DISPI_INDEX_ENABLE, 0x01 | 0x40 | 0x80);
}
#endif

#ifdef __aarch64__
#define multiboot_info_t void
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  Kernel boot banner — prints like Linux's "Linux version X.X.X"
 * ═══════════════════════════════════════════════════════════════════════ */
static void kernel_print_banner(void) {
    kprintf("\n");
    kprintf("============================================================\n");
    kprintf("  %s version %s (%s) (%s)\n", KERNEL_OS, KERNEL_VERSION,
            KERNEL_VERSION_STR, KERNEL_ARCH);
    kprintf("  Command line: (none)\n");
    kprintf("  BIOS/hardware provided memory info available.\n");
    kprintf("  %s kernel built with GCC %d.%d.%d\n", KERNEL_ARCH,
            __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    kprintf("============================================================\n");
    kprintf("\n");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Boot phase tracking — like Linux's printk times
 * ═══════════════════════════════════════════════════════════════════════ */
typedef struct {
    const char *name;
    uint64_t start_ms;
    uint64_t end_ms;
    int failed;
} boot_phase_t;

#define BOOT_MAX_PHASES 32
static boot_phase_t boot_log[BOOT_MAX_PHASES];
static int boot_log_idx = 0;

static void phase_start(boot_phase_t *ph, const char *name) {
    ph->name = name;
    ph->start_ms = timer_get_milliseconds();
    ph->end_ms = 0;
    ph->failed = 0;
}

static void phase_end(boot_phase_t *ph, int ok) {
    ph->end_ms = timer_get_milliseconds();
    ph->failed = !ok;
    if (boot_log_idx < BOOT_MAX_PHASES)
        boot_log[boot_log_idx++] = *ph;
}

static void boot_print_summary(void) {
    uint64_t total = boot_log_idx > 0 ?
        boot_log[boot_log_idx - 1].end_ms - boot_log[0].start_ms : 0;
    kprintf("\n=== Phase Summary (%llu ms total) ===\n", total);
    for (int i = 0; i < boot_log_idx; i++) {
        uint64_t ms = boot_log[i].end_ms - boot_log[i].start_ms;
        kprintf("  [%3llu.%03llus] %-30s %s\n",
                ms / 1000, ms % 1000, boot_log[i].name,
                boot_log[i].failed ? "FAILED" : "ok");
    }
    kprintf("========================================\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Initcall infrastructure — subsystem tables iterated at boot
 * ═══════════════════════════════════════════════════════════════════════ */
typedef int  (*initcall_fn)(void);
typedef void (*void_initcall_fn)(void);

typedef struct {
    const char *name;
    union {
        initcall_fn   rc;   /* returns int (0 = success) */
        void_initcall_fn v; /* returns void (assumed ok) */
    } fn;
    int returns_int;
} init_entry_t;

static int run_init_table(const init_entry_t *table, int count, const char *phase_name) {
    int failed = 0;
    for (int i = 0; i < count; i++) {
        int rc;
        if (table[i].returns_int)
            rc = table[i].fn.rc();
        else {
            table[i].fn.v();
            rc = 0;
        }
        if (rc != 0) {
            kprintf("  %s: %s failed (rc=%d)\n", phase_name, table[i].name, rc);
            failed++;
        }
    }
    return failed == 0;
}

/* ─── Phase 2: CPU & Interrupts ─── */
static const init_entry_t cpu_initcalls[] = {
    { "idt",        { .v = idt_init },            0 },
    { "apic",       { .v = apic_init },           0 },
    { "rng",        { .v = rng_init },            0 },
    { "syscall",    { .v = syscall_init },        0 },
    { "gdt_tss",    { .v = gdt_tss_init },        0 },
    { "sched",      { .v = sched_init },          0 },
};

/* ─── Phase 3: Memory management ─── */
static const init_entry_t mm_initcalls[] = {
    { "slab",       { .v = slab_init },           0 },
};

/* ─── Phase 4: Core kernel subsystems ─── */
static const init_entry_t core_initcalls[] = {
    { "script",     { .v = script_init },         0 },
    { "csl",        { .rc = csl_init },           1 },
    { "ipc",        { .v = ipc_init },            0 },
    { "audit",      { .v = audit_init },           0 },
    { "inotify",    { .rc = inotify_init },       1 },
    { "namespace",  { .rc = ns_init },            1 },
    { "vfs",        { .v = vfs_init },            0 },
    { "dns",        { .v = dns_init },            0 },
};

/* ─── Phase 7: Networking stack ─── */
static const init_entry_t net_initcalls[] = {
    { "eth",        { .rc = eth_init },           1 },
    { "arp",        { .rc = arp_init },           1 },
    { "ip",         { .rc = ip_init },            1 },
    { "udp",        { .rc = udp_init },           1 },
    { "icmp",       { .rc = icmp_init },          1 },
    { "tcp",        { .v = tcp_init },             0 },
    { "netdev",     { .rc = netdev_init },        1 },
    { "socket",     { .rc = socket_init },        1 },
};

/* ─── Phase 8: Security & compat ─── */
static const init_entry_t security_initcalls[] = {
    { "clamav",     { .v = clamav_init },         0 },
    { "sec",        { .v = sec_init },            0 },
    { "adblock",    { .v = adblock_init },        0 },
    { "android",    { .v = android_init },        0 },
    { "android_display", { .rc = android_display_init }, 1 },
    { "android_prop",    { .rc = android_prop_init },    1 },
    { "android_rc",      { .rc = android_rc_init },      1 },
    { "android_container", { .rc = android_container_init }, 1 },
    { "container",  { .rc = container_init },     1 },
    { "vm_manager", { .rc = vm_manager_init },    1 },
    { "appvm",      { .rc = appvm_init },         1 },
    { "adb",        { .v = adb_init },            0 },
    { "updater",    { .v = updater_init },        0 },
    { "zircon",     { .v = zircon_init },         0 },
    { "display_bridge", { .rc = display_bridge_init }, 1 },
    { "x11",        { .v = x11_server_init },    0 },
};

/* ─── Phase 9: Audio ─── */
static const init_entry_t audio_initcalls[] = {
    { "audio",      { .v = audio_init },          0 },
    { "mixer",      { .v = mixer_init },          0 },
};

/* ═══════════════════════════════════════════════════════════════════════
 *  Security feature probe — SMEP, SMAP, KPTI
 * ═══════════════════════════════════════════════════════════════════════ */
#ifndef __aarch64__
static void security_features_enable(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    uint64_t cr4;
    __asm__("mov %%cr4, %0" : "=r"(cr4));
    if (ebx & (1 << 7))  cr4 |= (1ULL << 20);  /* SMEP */
    if (ebx & (1 << 20)) cr4 |= (1ULL << 21);  /* SMAP */
    __asm__("mov %0, %%cr4" : : "r"(cr4));
    kprintf("  SMEP %s, SMAP %s\n",
            (ebx & (1 << 7))  ? "on" : "off",
            (ebx & (1 << 20)) ? "on" : "off");
}
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  Kernel static constructors (.preinit_array / .init_array)
 *  Qt6 registers runtime init via Q_CONSTRUCTOR_FUNCTION — must run
 *  before first Qt use or QImage::fill tail-calls address 0.
 * ═══════════════════════════════════════════════════════════════════════ */
static void kernel_run_constructors(void) {
    for (ctor_fn_t *fn = __preinit_array_start; fn < __preinit_array_end; fn++)
        (*fn)();
    for (ctor_fn_t *fn = __init_array_start; fn < __init_array_end; fn++)
        (*fn)();
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Kernel halt / reboot — clean system shutdown
 * ═══════════════════════════════════════════════════════════════════════ */
void kernel_halt(void) {
    kprintf("\n%s: System halted.\n", KERNEL_OS);
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

void kernel_reboot(void) {
    kprintf("\n%s: Rebooting...\n", KERNEL_OS);
    /* Triple-fault reset via keyboard controller */
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    kernel_halt();
}

void kernel_poweroff(void) {
    kprintf("\n%s: Powering off...\n", KERNEL_OS);
    /* ACPI power-off (QEMU-friendly) */
    outw(0x604, 0x2000);
    /* Fallback: APM */
    outw(0xB004, 0x2000);
    kernel_halt();
}

/* ═══════════════════════════════════════════════════════════════════════
 *  panic — kernel oops
 * ═══════════════════════════════════════════════════════════════════════ */
void kernel_panic(const char *fmt, ...) {
    kprintf("\n*** KERNEL PANIC: ");
    /* Simple varargs forwarding — fmt is already a kprintf-compatible string */
    kprintf("%s", fmt);
    kprintf(" ***\n");
    kprintf("Halting system.\n");
    kernel_halt();
}

/* ═══════════════════════════════════════════════════════════════════════
 *                      MAIN ENTRY POINT
 * ═══════════════════════════════════════════════════════════════════════ */
void kernel_main(uint32_t magic __attribute__((unused)),
                 multiboot_info_t *mb_info __attribute__((unused))) {

    boot_phase_t ph;

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 0: Early console — serial only, no output redirection
     * ───────────────────────────────────────────────────────────────── */
    serial_init();
    kprintf_set_serial(serial_putchar);

    /* Parse boot command line (recovery/safemode flags) */
    {
        const char *cl = limine_get_cmdline();
        if (cl && cl[0]) {
            if (strstr(cl, "recovery")) {
                extern int kernel_recovery_mode;
                kernel_recovery_mode = 1;
                kprintf("boot: RECOVERY mode requested via cmdline\n");
            }
            if (strstr(cl, "safemode")) {
                extern int kernel_safe_mode;
                kernel_safe_mode = 1;
                kprintf("boot: SAFE MODE requested via cmdline\n");
            }
            if (strstr(cl, "httpsboot")) {
                extern int g_https_boot_test;
                g_https_boot_test = 1;
                kprintf("boot: HTTPS BOOT TEST requested via cmdline\n");
            }
            /* net.ip=w.x.y.z → static IP override (multi-instance testing) */
            {
                const char *ip = strstr(cl, "net.ip=");
                if (ip) {
                    const char *p = ip + 7;
                    unsigned octets[4];
                    int n = 0;
                    while (n < 4 && *p >= '0' && *p <= '9') {
                        unsigned v = 0;
                        while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned)(*p++ - '0');
                        if (v > 255) break;
                        octets[n++] = v;
                        if (*p == '.') p++;
                    }
                    if (n == 4) {
                        extern int net_override_ip;
                        net_override_ip = (octets[0] << 24) | (octets[1] << 16) |
                                          (octets[2] << 8) | octets[3];
                        kprintf("boot: static IP override %u.%u.%u.%u\n",
                                octets[0], octets[1], octets[2], octets[3]);
                    }
                }
            }
        }
    }

    /* Print kernel version banner (like Linux's "Linux version X.X.X") */
    kernel_print_banner();

    int has_fb = 0;
    uint64_t pci_fb_addr = 0;
    uint32_t pci_fb_pitch = 0;
    int pci_fb_found = 0;

    if (limine_booted()) {
        phys_to_virt_base = limine_hhdm_offset();
        has_fb = limine_setup_fb() == 0;
    }

    if (!has_fb) {
        pci_init();
        pci_scan();
    }

    gpu_backend_t gpu_backend = gpu_init();
    if (!has_fb && gpu_backend == GPU_BACKEND_VIRTIO) {
        has_fb = fb_init(gpu_get_framebuffer_phys(),
                         gpu_get_width(), gpu_get_height(),
                         gpu_get_pitch(), gpu_get_bpp(), 0);
        if (has_fb) {
            kprintf_set_output(fb_putchar, fb_clear);
            fb_cursor_init();
            fb_cursor_show();
        }
    }

    if (!has_fb) {
        uint32_t hdmi_w = 0, hdmi_h = 0;
        if (hdmi_detect(&pci_fb_addr, &hdmi_w, &hdmi_h, &pci_fb_pitch))
            pci_fb_found = 1;
    }

    fpu_init_early();

    /* Suppress serial text output during bootsplash overlay */
    kprintf_disable_output();

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 1: Memory — PMM, VMM, heap, slab cache
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "memory");
    uint64_t mem_size = 256 * 1024 * 1024;

    if (limine_booted()) {
        limine_setup_mm(&mem_size);
    } else {
        mm_init(0, 262144, 0, 0);
        mem_size = 256ULL * 1024 * 1024;
    }

    pmm_init(mem_size, (uint64_t)_kernel_start, (uint64_t)_kernel_end);
    vmm_init();
    extern char __tls_end[];
    *(uint64_t *)__tls_end = (uint64_t)__tls_end;
    mm_heap_preseed(64);
    bootsplash_set_progress(12, "Memory ready");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 2: CPU, interrupts, GDT, IDT, APIC, scheduler
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "cpu");

#ifdef __aarch64__
    rtc_init();
    rng_init();
    idt_init();
    syscall_init();
    gdt_tss_init();
    sched_init();
    idt_enable();
#else
    security_features_enable();
    run_init_table(cpu_initcalls, sizeof(cpu_initcalls)/sizeof(cpu_initcalls[0]), "cpu");
    syscall_msr_init(syscall_entry);
#endif

    bootsplash_set_progress(20, "CPU ready");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 3: Framebuffer — PCI fallback, VBE setup, bootsplash
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "framebuffer");

#ifndef __aarch64__
    idt_enable();

    if (!has_fb && !limine_booted() && pci_fb_found) {
        uint64_t fb_size = 1920 * 1080 * 4;
        for (uint64_t off = 0; off < fb_size; off += 0x1000) {
            vmm_map_page(pci_fb_addr + off, pci_fb_addr + off,
                         PAGE_PRESENT | PAGE_WRITE);
        }
        pci_fb_pitch = 1920 * 4;
        vbe_set_mode(1920, 1080, 32);
        if (fb_init(pci_fb_addr, 1920, 1080, pci_fb_pitch, 32, 0)) {
            has_fb = 1;
            kprintf_set_output(fb_putchar, fb_clear);
            fb_cursor_init();
            fb_cursor_show();
        }
    }
#endif

    if (has_fb) {
        fb_clear();
        fb_cursor_hide();
        bootsplash_init();
        bootsplash_set_progress(5, "Starting " KERNEL_OS "...");
    }
    phase_end(&ph, has_fb);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 4: Core kernel subsystems — slab, IPC, VFS, scripting,
     *            capabilities, audit, inotify, namespaces
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "kernel_core");
    run_init_table(mm_initcalls, sizeof(mm_initcalls)/sizeof(mm_initcalls[0]), "mm");
    run_init_table(core_initcalls, sizeof(core_initcalls)/sizeof(core_initcalls[0]), "core");
    bootsplash_set_progress(35, "Core kernel ready");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 5: Hardware drivers — timer, input, USB, WiFi, audio
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "drivers");
    drivers_init();
    run_init_table(audio_initcalls, sizeof(audio_initcalls)/sizeof(audio_initcalls[0]), "audio");
    drivers_print_status();
    bootsplash_set_progress(45, "Drivers loaded");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 6: Networking — legacy monolith + new modular stack
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "network");
    if (net_init() < 0) {
        kprintf("  net: legacy init failed (no NIC)\n");
    } else {
        run_init_table(net_initcalls,
                       sizeof(net_initcalls)/sizeof(net_initcalls[0]), "net");
    }

    extern int wifi_init(void);
    if (wifi_init() == 0)
        kprintf("  wifi: connected\n");

    /* net_selftest() disabled: it hammers the host test server (10.0.2.2:9001)
     * which has no listener in a normal boot, holding the single TCP socket
     * and stalling later connects (e.g. OpenWeb). */
    /* OpenWeb form/HTTP smoke test against the host demo server — only
     * enabled when the host has a listener on 10.0.2.2:9001. */
#if 0
    extern void net_form_smoke(void);
    net_form_smoke();
#endif
    netstat_init();
    bootsplash_set_progress(55, "Network ready");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 7: Storage — block devices, partitions, ext2, initramfs
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "storage");
    if (block_init()) {
        int np = part_init();
        if (np > 0) {
            for (int i = 0; i < np; i++) {
                partition_t p;
                part_get(i, &p);
                if (p.type == 0x07) {
                    /* ExFAT boot partition — Limine's built-in ExFAT driver
                     * boots the kernel from here; not mounted at runtime. */
                    kprintf("    boot: exfat @ LBA %u (%u sectors)\n",
                            p.start_lba, p.sector_count);
                } else if (p.type == 0x83) {
                    if (codefs_mount(i) == 0) {
                        script_run_file("/scripts/test1.script");
                        script_run_file("/scripts/complex.script");
                    } else if (ext2_mount(i)) {
                        script_run_file("/scripts/test1.script");
                        script_run_file("/scripts/complex.script");
                    }
                }
            }
            bootsplash_set_progress(68, "Filesystem mounted");
        }
    } else {
        kprintf("  storage: no block device found\n");
    }

    fs_init();
    initramfs_populate();
    rootfs_seed_android_stock();
    bootsplash_set_progress(72, "Storage ready");
    phase_end(&ph, 1);

/* Linux-ABI syscall coverage probe (spike). Spawns a userspace
     * /bin/linux-probe process running under the Linux personality
     * translation — only when explicitly enabled (host tooling). */
 #if 1
    extern int compat_probe_run(void);
    extern int compat_probe_finished(void);
    compat_probe_run();
    {
        /* Belt-and-suspenders: never let a stalled probe hold up boot.
         * The probe is diagnostic only, so give it a bounded window and
         * carry on if it hasn't reported back. */
        uint64_t lp_start = timer_get_milliseconds();
        while (!compat_probe_finished() &&
               timer_get_milliseconds() - lp_start < 10000)
            sched_yield();
        if (!compat_probe_finished())
            kprintf("LPROBE: timed out; continuing boot\n");
    }
#endif

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 8: Security, compat, virtualization
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "security_compat");
    run_init_table(security_initcalls,
                   sizeof(security_initcalls)/sizeof(security_initcalls[0]),
                   "sec");
    bootsplash_set_progress(78, "Security & compat ready");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 9: Userspace — packages, initramfs, Qt constructors
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "userspace");
    pkg_init();
    extern void pkg_b64_selftest(void);
    pkg_b64_selftest();
    /* pkg_net_selftest() disabled (temporary harness, see above). */
    shell_selftest();
    extern int g_https_boot_test;
    if (g_https_boot_test) {
        extern void https_boot_test(void);
        https_boot_test();
    }
    bootsplash_set_progress(82, "Apps loaded");
    phase_end(&ph, 1);

    /* ─────────────────────────────────────────────────────────────────
     *  PHASE 10: Qt6 static constructors + desktop launch
     * ───────────────────────────────────────────────────────────────── */
    phase_start(&ph, "desktop");
    kernel_run_constructors();

    if (has_fb) {
        bootsplash_tick();
        bootsplash_set_progress(95, "Starting desktop...");
    }

    boot_print_summary();

    /* ─────────────────────────────────────────────────────────────────
     *  LAUNCH: Qt6 liquid-glass desktop (or fallback kernel shell)
     * ───────────────────────────────────────────────────────────────── */
    if (has_fb) {
        /* Wire the X11 compositor into the desktop render path. */
        extern void xs_init(void);
        xs_init();
        /* Arm the user-window bridge (Android apps -> LVGL windows). */
        extern void user_wm_init(void);
        user_wm_init();
    }
    if (has_fb && qt_desktop_init()) {
        bootsplash_set_progress(100, "Ready!");
        bootsplash_finish();
        qt_desktop_run();
    }

    /* Fallback: kernel console shell */
    shell_init();
    shell_run();

    /* If shell exits, clean halt */
    kernel_halt();
}
