#include "installer_qt.h"
#include "block.h"
#include "part.h"
#include "kprintf.h"
#include "string.h"
#include "timer.h"
#include "io.h"
#include "exfat_mkfs.h"
#include "codefs_mkfs.h"
#include "codefs.h"
#include "limine-bios-hdd.h"

/* Offset in the stage1 MBR where limine bios-install stores the install
 * marker byte; 0x02 means "stage2 present in the MBR gap". */
#define STAGE1_INSTALL_FLAG_OFFSET 421

/* ──────────────────────────────────────────────────────────────
   Progress reporting — polled by the Qt GUI every tick
   ────────────────────────────────────────────────────────────── */

static installer_progress_t g_installer;

void installer_reset_progress(void) {
    memset(&g_installer, 0, sizeof(g_installer));
}

installer_progress_t *installer_get_progress(void) {
    return &g_installer;
}

static void ip_set_step(int step, const char *name) {
    g_installer.step = step;
    strncpy_safe(g_installer.step_name, name, sizeof(g_installer.step_name) - 1);
}

static void ip_set_percent(int pct) { g_installer.percent = pct; }

static void ip_log(const char *msg) {
    if (g_installer.log_lines < INSTALLER_LOG_LINES) {
        strncpy_safe(g_installer.log[g_installer.log_lines], msg, INSTALLER_LOG_LEN - 1);
        g_installer.log_lines++;
    }
}

static void ip_finish(int error) {
    g_installer.done = 1;
    g_installer.error = error;
    ip_log(error ? "Installation failed!" : "Installation complete!");
}

/* ──────────────────────────────────────────────────────────────
   User config, WiFi config, locale
   ────────────────────────────────────────────────────────────── */

static char cfg_full_name[INSTALLER_NAME_LEN];
static char cfg_username[INSTALLER_NAME_LEN];
static char cfg_password[INSTALLER_PASS_LEN];
static char cfg_wifi_ssid[INSTALLER_SSID_LEN];
static char cfg_wifi_pass[INSTALLER_PASS_LEN];
static char cfg_locale[16] = "en_US";

void installer_set_user(const char *full_name, const char *username, const char *password) {
    strncpy_safe(cfg_full_name, full_name ? full_name : "", INSTALLER_NAME_LEN - 1);
    strncpy_safe(cfg_username, username ? username : "user", INSTALLER_NAME_LEN - 1);
    strncpy_safe(cfg_password, password ? password : "", INSTALLER_PASS_LEN - 1);
}

void installer_set_wifi(const char *ssid, const char *password) {
    strncpy_safe(cfg_wifi_ssid, ssid ? ssid : "", INSTALLER_SSID_LEN - 1);
    strncpy_safe(cfg_wifi_pass, password ? password : "", INSTALLER_PASS_LEN - 1);
}

void installer_set_locale(const char *locale) {
    strncpy_safe(cfg_locale, locale ? locale : "en_US", sizeof(cfg_locale) - 1);
}

/* ──────────────────────────────────────────────────────────────
   Simple hash for password storage (FNV-1a, not cryptographic)
   ────────────────────────────────────────────────────────────── */

static uint32_t simple_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

/* ──────────────────────────────────────────────────────────────
   Disk detection
   ────────────────────────────────────────────────────────────── */

void installer_detect_disk(void) {
    installer_reset_progress();
    if (block_available()) {
        int sectors = 0, is_lba = 0;
        block_get_info(&sectors, &is_lba);
        g_installer.disk_available = 1;
        g_installer.disk_sectors = sectors;
        g_installer.disk_is_lba = is_lba;
        uint64_t bytes = (uint64_t)sectors * 512;
        if (bytes >= 1073741824ULL)
            snprintf(g_installer.disk_size_str, sizeof(g_installer.disk_size_str),
                     "%llu GB", (unsigned long long)(bytes / 1073741824ULL));
        else if (bytes >= 1048576ULL)
            snprintf(g_installer.disk_size_str, sizeof(g_installer.disk_size_str),
                     "%llu MB", (unsigned long long)(bytes / 1048576ULL));
        else
            snprintf(g_installer.disk_size_str, sizeof(g_installer.disk_size_str),
                     "%llu KB", (unsigned long long)(bytes / 1024ULL));
    }
}

/* ──────────────────────────────────────────────────────────────
   Partition layout (dual-partition install):
     PE1: bootable ExFAT  (0x07) @ LBA 2048, 1 GiB  — boot partition
     PE2: CodeFS (CPF)    (0x83) @ rest of disk     — data/home partition
   The 1 GiB boot partition geometry matches the validated ExFAT
   boot volume (Limine's custom read-only ExFAT driver boots from it).
   ────────────────────────────────────────────────────────────── */

#define BOOT_PART_START_LBA 2048u
#define BOOT_PART_SECTORS   2095104u       /* 1 GiB */

static int boot_part = -1;
static int data_part = -1;

static void pe_fill(uint8_t *pe, uint8_t bootable, uint8_t type,
                    uint64_t start_lba, uint64_t sector_count) {
    memset(pe, 0, 16);
    pe[0] = bootable; pe[4] = type;
    pe[1] = 0xFE; pe[2] = 0xFF; pe[3] = 0xFF;
    pe[5] = 0xFE; pe[6] = 0xFF; pe[7] = 0xFF;
    for (int i = 0; i < 4; i++) pe[8 + i]  = (uint8_t)(start_lba >> (8 * i));
    for (int i = 0; i < 4; i++) pe[12 + i] = (uint8_t)(sector_count >> (8 * i));
}

/* Write the two partition entries onto the current sector 0 image.  The Limine
 * BIOS boot blob shipped in limine-bios-hdd.h has EMPTY partition entries, so
 * this must be re-run after install_step_bootloader clobbers the table
 * (mirrors real limine-deploy, which preserves existing entries). */
static int write_mbr_table(void) {
    if (!block_available()) return -1;
    int sectors = 0, is_lba = 0;
    block_get_info(&sectors, &is_lba);
    if (sectors <= 0) return -1;

    uint64_t data_start = BOOT_PART_START_LBA + BOOT_PART_SECTORS;
    uint64_t data_sectors = ((uint64_t)sectors > data_start) ? (uint64_t)sectors - data_start : 0;
    if (data_sectors == 0) return -1;

    uint8_t mbr[512];
    if (block_read_sectors(0, 1, mbr) < 0)
        memset(mbr, 0, 512);

    memset(mbr + 0x1BE, 0, 64);
    pe_fill(mbr + 0x1BE, 0x80, 0x07, BOOT_PART_START_LBA, BOOT_PART_SECTORS);
    pe_fill(mbr + 0x1CE, 0x00, 0x83, data_start, data_sectors);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;

    return block_write_sectors(0, 1, mbr) < 0 ? -1 : 0;
}

/* ──────────────────────────────────────────────────────────────
   Main installation pipeline
   ────────────────────────────────────────────────────────────── */

static int install_step_partition(void) {
    ip_set_step(1, "Creating partition table...");
    ip_set_percent(5);
    ip_log("Writing dual-partition MBR...");
    if (write_mbr_table() < 0) {
        ip_log("Failed to write MBR");
        return -1;
    }
    ip_log("MBR: boot (ExFAT) + data (CodeFS) created");
    ip_set_percent(15);
    return 0;
}

static int install_step_detect(void) {
    ip_set_step(2, "Detecting partitions...");
    part_init();
    boot_part = -1;
    data_part = -1;
    for (int i = 0; i < part_count(); i++) {
        partition_t p;
        part_get(i, &p);
        if (p.type == 0x07 && boot_part < 0) boot_part = i;
        else if (p.type == 0x83 && data_part < 0) data_part = i;
    }
    if (boot_part < 0 || data_part < 0) {
        ip_log("No boot or data partition found");
        return -1;
    }
    ip_log("Boot + data partitions detected");
    ip_set_percent(25);
    return 0;
}

static int install_step_format_boot(void) {
    ip_set_step(3, "Formatting boot partition (ExFAT)...");
    ip_log("Creating ExFAT boot volume...");

    __attribute__((weak)) extern uint8_t _binary_codeos_1_kernel_stage1_bin_start[];
    __attribute__((weak)) extern uint8_t _binary_codeos_1_kernel_stage1_bin_end[];
    __attribute__((weak)) extern uint8_t _binary_bootloader_limine_conf_start[];
    __attribute__((weak)) extern uint8_t _binary_bootloader_limine_conf_end[];
    __attribute__((weak)) extern uint8_t _binary_bootloader_splash_png_start[];
    __attribute__((weak)) extern uint8_t _binary_bootloader_splash_png_end[];
    __attribute__((weak)) extern uint8_t _binary_bootloader_limine_bios_sys_start[];
    __attribute__((weak)) extern uint8_t _binary_bootloader_limine_bios_sys_end[];

    exfat_mkfs_file_t boot_files[4];
    exfat_mkfs_file_t limine_files[1];
    int nb = 0;

    int lsize = 0;
    if ((uintptr_t)_binary_bootloader_limine_bios_sys_start != 0) {
        lsize = (int)(_binary_bootloader_limine_bios_sys_end - _binary_bootloader_limine_bios_sys_start);
    }

    if ((uintptr_t)_binary_codeos_1_kernel_stage1_bin_start != 0) {
        int ksize = (int)(_binary_codeos_1_kernel_stage1_bin_end - _binary_codeos_1_kernel_stage1_bin_start);
        if (ksize > 0) {
            boot_files[nb].name = "codeos-1-kernel.bin";
            boot_files[nb].data = _binary_codeos_1_kernel_stage1_bin_start;
            boot_files[nb].size = (uint64_t)ksize;
            nb++;
        }
    }
    if (lsize > 0) {
        boot_files[nb].name = "limine-bios.sys";
        boot_files[nb].data = _binary_bootloader_limine_bios_sys_start;
        boot_files[nb].size = (uint64_t)lsize;
        nb++;
    }
    if ((uintptr_t)_binary_bootloader_limine_conf_start != 0) {
        int csize = (int)(_binary_bootloader_limine_conf_end - _binary_bootloader_limine_conf_start);
        if (csize > 0) {
            boot_files[nb].name = "limine.conf";
            boot_files[nb].data = _binary_bootloader_limine_conf_start;
            boot_files[nb].size = (uint64_t)csize;
            nb++;
        }
    }
    if ((uintptr_t)_binary_bootloader_splash_png_start != 0) {
        int ssize = (int)(_binary_bootloader_splash_png_end - _binary_bootloader_splash_png_start);
        if (ssize > 0) {
            boot_files[nb].name = "splash.png";
            boot_files[nb].data = _binary_bootloader_splash_png_start;
            boot_files[nb].size = (uint64_t)ssize;
            nb++;
        }
    }
    if (boot_part < 0) return -1;

    int nl = 0;
    if (lsize > 0) {
        limine_files[nl].name = "limine-bios.sys";
        limine_files[nl].data = _binary_bootloader_limine_bios_sys_start;
        limine_files[nl].size = (uint64_t)lsize;
        nl++;
    }

    if (exfat_mkfs_volume(boot_part, boot_files, nb, limine_files, nl) < 0) {
        ip_log("Failed to format ExFAT boot volume");
        return -1;
    }
    ip_log("ExFAT boot volume created");
    ip_set_percent(45);
    return 0;
}

static int install_step_format_data(void) {
    ip_set_step(4, "Formatting data partition (CodeFS)...");
    ip_log("Creating CodeFS data volume...");
    if (codefs_mkfs(data_part) < 0) {
        ip_log("Failed to format CodeFS data volume");
        return -1;
    }
    ip_log("CodeFS data volume created");
    ip_set_percent(60);
    return 0;
}

static int install_step_mount(void) {
    ip_set_step(5, "Mounting data partition...");
    ip_log("Mounting CodeFS...");
    if (codefs_mount(data_part) < 0) {
        ip_log("Failed to mount CodeFS");
        return -1;
    }
    ip_log("CodeFS mounted");
    ip_set_percent(70);
    return 0;
}

static int install_step_user_config(void) {
    ip_set_step(6, "Setting up user account...");
    char user_line[256];
    uint32_t phash = simple_hash(cfg_password);
    snprintf(user_line, sizeof(user_line), "%s:%x:%s:1000:1000\n",
             cfg_username[0] ? cfg_username : "user",
             phash,
             cfg_full_name[0] ? cfg_full_name : "User");
    if (codefs_write_file("/etc/users.conf", user_line, strlen(user_line)) < 0) {
        ip_log("Failed to write user config");
        return -1;
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "User '%s' created", cfg_username[0] ? cfg_username : "user");
    ip_log(buf);

    if (cfg_wifi_ssid[0]) {
        char wifi_line[256];
        snprintf(wifi_line, sizeof(wifi_line), "%s:%s\n", cfg_wifi_ssid, cfg_wifi_pass);
        if (codefs_write_file("/etc/wifi.conf", wifi_line, strlen(wifi_line)) < 0)
            ip_log("Failed to write WiFi config");
        else
            ip_log("WiFi configuration saved");
    }

    if (codefs_write_file("/etc/locale.conf", cfg_locale, strlen(cfg_locale)) < 0)
        ip_log("Failed to write locale");
    else {
        char lbuf[80];
        snprintf(lbuf, sizeof(lbuf), "Locale set to %s", cfg_locale);
        ip_log(lbuf);
    }
    codefs_sync();
    ip_log("Configuration written");
    ip_set_percent(85);
    return 0;
}

static int install_step_bootloader(void) {
    ip_set_step(7, "Installing bootloader...");
    int boot_code_len = sizeof(binary_limine_hdd_bin_data);
    if (boot_code_len > 512) {
        if (block_write_sectors(0, 1, binary_limine_hdd_bin_data) < 0) {
            ip_log("Failed to write bootloader MBR");
            return -1;
        }
        int remaining = boot_code_len - 512;
        int extra_sectors = (remaining + 511) / 512;
        const uint8_t *extra = binary_limine_hdd_bin_data + 512;
        for (int s = 0; s < extra_sectors; s++) {
            uint8_t sector[512];
            memset(sector, 0, 512);
            int copy = remaining - s * 512;
            if (copy > 512) copy = 512;
            if (copy > 0) memcpy(sector, extra + s * 512, copy);
            if (block_write_sectors(1 + s, 1, sector) < 0) {
                ip_log("Failed to write bootloader stage2");
                return -1;
            }
        }
        /* The limine blob clobbers the partition table; restore it. */
        if (write_mbr_table() < 0) {
            ip_log("Failed to restore partition table");
            return -1;
        }
        /* Bump the install marker byte (limine bios-install sets 0x02 here)
         * so the stage1 hands off to the stage2 in the MBR gap. */
        uint8_t mbr[512];
        if (block_read_sectors(0, 1, mbr) == 0) {
            mbr[STAGE1_INSTALL_FLAG_OFFSET] = 0x02;
            block_write_sectors(0, 1, mbr);
        }
        char buf[80];
        snprintf(buf, sizeof(buf), "Limine bootloader installed (%d sectors)", 1 + extra_sectors);
        ip_log(buf);
    } else {
        if (block_write_sectors(0, 1, binary_limine_hdd_bin_data) < 0) {
            ip_log("Failed to write bootloader");
            return -1;
        }
        if (write_mbr_table() < 0) {
            ip_log("Failed to restore partition table");
            return -1;
        }
        ip_log("Limine bootloader installed");
    }
    ip_set_percent(95);
    return 0;
}

static void do_install_steps(void) {
    installer_reset_progress();
    g_installer.total_steps = INSTALLER_TOTAL_STEPS;
    g_installer.disk_available = 1;
    int sectors = 0, is_lba = 0;
    block_get_info(&sectors, &is_lba);
    g_installer.disk_sectors = sectors;
    g_installer.disk_is_lba = is_lba;
    uint64_t bytes = (uint64_t)sectors * 512;
    if (bytes >= 1073741824ULL)
        snprintf(g_installer.disk_size_str, sizeof(g_installer.disk_size_str),
                 "%llu GB", (unsigned long long)(bytes / 1073741824ULL));
    else if (bytes >= 1048576ULL)
        snprintf(g_installer.disk_size_str, sizeof(g_installer.disk_size_str),
                 "%llu MB", (unsigned long long)(bytes / 1048576ULL));
    else
        snprintf(g_installer.disk_size_str, sizeof(g_installer.disk_size_str),
                 "%llu KB", (unsigned long long)(bytes / 1024ULL));

    if (install_step_partition() < 0)     goto fail;
    if (install_step_detect() < 0)        goto fail;
    if (install_step_format_boot() < 0)   goto fail;
    if (install_step_format_data() < 0)   goto fail;
    if (install_step_mount() < 0)         goto fail;
    if (install_step_user_config() < 0)   goto fail;
    if (install_step_bootloader() < 0)    goto fail;

    ip_set_percent(100);
    ip_finish(0);
    return;

fail:
    ip_finish(1);
}

void installer_run(void) {
    if (!block_available()) {
        installer_reset_progress();
        ip_log("Error: no disk detected");
        ip_finish(1);
        return;
    }
    do_install_steps();
}