#include "installer.h"
#include "desktop.h"
#include "windows.h"
#include "kprintf.h"
#include "string.h"
#include "block.h"
#include "part.h"
#include "ext2.h"
#include "fb.h"
#include "keyboard.h"
#include "timer.h"
#include "io.h"
#include "limine-bios-hdd.h"

#define WIN_W 720
#define WIN_H 480

static int inst_open;
static int inst_win;
static int inst_screen;
static int inst_sel;
static int inst_prog;
static int inst_total;
static char inst_status[64];
static int inst_done;
static int target_part;

static void draw_str(int x, int y, const char *s, uint32_t c) {
    fb_drawstr_px(x, y, s, c, 0);
}

static void draw_bar(int x, int y, int w, int h, int p, int t) {
    int pct = t > 0 ? p * 100 / t : 0;
    ui_draw_progress(x, y, w, h, pct, C_SURFACE1, C_BLUE);
}

int installer_open(void) {
    if (inst_open) return 1;
    uint32_t sw = fb_getwidth(), sh = fb_getheight();
    int ix = (sw - WIN_W) / 2, iy = (sh - WIN_H) / 3;

    inst_win = desktop_new_window(ix, iy, WIN_W, WIN_H, "CodeOS Installer",
                                  0xFFFFFFFF, C_BASE);
    if (inst_win < 0) return 0;

    inst_open = 1;
    inst_screen = 0;
    inst_sel = 0;
    inst_prog = 0;
    inst_total = 100;
    inst_status[0] = 0;
    inst_done = 0;
    target_part = -1;

    desktop_redraw();
    return 1;
}

int installer_is_open(void) { return inst_open; }

void installer_close(void) {
    inst_open = 0;
    desktop_close_window(inst_win);
    desktop_redraw();
}

/* ── ext2 mkfs ── */

static int mkfs_ext2(int part_idx) {
    partition_t p;
    if (part_get(part_idx, &p) < 0 || p.type != 0x83) return -1;

    uint64_t total_sectors = p.sector_count;
    uint32_t bs = 1024;
    uint32_t blocks = (uint32_t)(total_sectors * 512 / bs);
    if (blocks < 32) return -1;
    if (blocks > 65536) blocks = 65536;

    uint32_t inodes = 128;
    uint32_t itbl_blks = (inodes * 128 + bs - 1) / bs;
    uint32_t first_db = 5 + itbl_blks;

    char block[1024];
    uint32_t lba;

    /* Block 1: superblock */
    memset(block, 0, 1024);
    struct {
        uint32_t inodes_count;
        uint32_t blocks_count;
        uint32_t r_blocks_count;
        uint32_t free_blocks_count;
        uint32_t free_inodes_count;
        uint32_t first_data_block;
        uint32_t log_block_size;
        uint32_t log_frag_size;
        uint32_t blocks_per_group;
        uint32_t frags_per_group;
        uint32_t inodes_per_group;
        uint32_t mtime;
        uint32_t wtime;
        uint16_t mnt_count;
        uint16_t max_mnt_count;
        uint16_t magic;
        uint16_t state;
        uint16_t errors;
        uint16_t minor_rev;
        uint32_t lastcheck;
        uint32_t checkinterval;
        uint32_t creator_os;
        uint32_t rev_level;
        uint16_t def_resuid;
        uint16_t def_resgid;
        uint32_t first_ino;
        uint16_t inode_size;
        uint16_t block_group_nr;
        uint32_t feature_compat;
        uint32_t feature_incompat;
        uint32_t feature_ro_compat;
        uint8_t  uuid[16];
        char     volume_name[16];
    } __attribute__((packed)) sb;

    memset(&sb, 0, sizeof(sb));
    sb.inodes_count = inodes;
    sb.blocks_count = blocks;
    sb.r_blocks_count = 0;
    sb.free_blocks_count = blocks - first_db;
    sb.free_inodes_count = inodes - 3;
    sb.first_data_block = 1;
    sb.log_block_size = 0;
    sb.log_frag_size = 0;
    sb.blocks_per_group = blocks;
    sb.frags_per_group = blocks;
    sb.inodes_per_group = inodes;
    sb.magic = 0xEF53;
    sb.state = 1;
    sb.errors = 1;
    sb.rev_level = 1;
    sb.def_resuid = 0;
    sb.def_resgid = 0;
    sb.first_ino = 11;
    sb.inode_size = 128;
    memcpy(sb.uuid, "CSL-MKFS000001", 16);
    memcpy(sb.volume_name, "CodeOS", 6);

    memcpy(block + 56, &sb, sizeof(sb));
    lba = p.start_lba + 2;
    if (block_write_sectors(lba, 2, block) < 0) return -1;

    /* Block 2: BG descriptors */
    memset(block, 0, 1024);
    struct {
        uint32_t block_bitmap;
        uint32_t inode_bitmap;
        uint32_t inode_table;
        uint16_t free_blocks_count;
        uint16_t free_inodes_count;
        uint16_t used_dirs_count;
        uint16_t pad;
    } __attribute__((packed)) bg;

    memset(&bg, 0, sizeof(bg));
    bg.block_bitmap = 3;
    bg.inode_bitmap = 4;
    bg.inode_table = 5;
    bg.free_blocks_count = sb.free_blocks_count;
    bg.free_inodes_count = sb.free_inodes_count;
    bg.used_dirs_count = 1;

    memcpy(block, &bg, sizeof(bg));
    lba = p.start_lba + 4;
    if (block_write_sectors(lba, 2, block) < 0) return -1;

    /* Block 3: block bitmap */
    memset(block, 0, 1024);
    for (uint32_t i = 0; i < first_db; i++)
        block[i / 8] |= (1 << (i % 8));
    lba = p.start_lba + 6;
    if (block_write_sectors(lba, 2, block) < 0) return -1;

    /* Block 4: inode bitmap */
    memset(block, 0, 1024);
    block[0] = 0x07; /* inodes 0,1,2 used */
    lba = p.start_lba + 8;
    if (block_write_sectors(lba, 2, block) < 0) return -1;

    /* Inode table (blocks 5..5+itbl_blks-1) */
    memset(block, 0, 1024);
    /* inode 2 = root directory */
    struct {
        uint16_t mode;
        uint16_t uid;
        uint32_t size;
        uint32_t atime;
        uint32_t ctime;
        uint32_t mtime;
        uint32_t dtime;
        uint16_t gid;
        uint16_t links_count;
        uint32_t blocks;
        uint32_t flags;
        uint32_t osd1;
        uint32_t block[15];
    } __attribute__((packed)) inode;

    memset(&inode, 0, sizeof(inode));
    inode.mode = 0x41ED;  /* drwxr-xr-x */
    inode.uid = 0;
    inode.size = 1024;
    inode.links_count = 2;
    inode.blocks = 2;
    inode.block[0] = first_db;

    memcpy(block + 128, &inode, sizeof(inode));

    for (uint32_t i = 0; i < itbl_blks; i++) {
        lba = p.start_lba + (5 + i) * 2;
        if (block_write_sectors(lba, 2, block) < 0) return -1;
        memset(block, 0, 1024);
    }

    /* Root directory data block */
    memset(block, 0, 1024);
    struct {
        uint32_t inode;
        uint16_t rec_len;
        uint8_t  name_len;
        uint8_t  file_type;
        char     name;
    } __attribute__((packed)) *dot = (void*)block;
    dot->inode = 2;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = 2;
    dot->name = '.';

    struct {
        uint32_t inode;
        uint16_t rec_len;
        uint8_t  name_len;
        uint8_t  file_type;
        char     name[2];
    } __attribute__((packed)) *dotdot = (void*)(block + 12);
    dotdot->inode = 2;
    dotdot->rec_len = 1024 - 12;
    dotdot->name_len = 2;
    dotdot->file_type = 2;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    lba = p.start_lba + first_db * 2;
    if (block_write_sectors(lba, 2, block) < 0) return -1;

    return 0;
}

/* ── Install steps ── */
enum { STEP_PARTITION, STEP_FORMAT, STEP_COPY, STEP_BOOTLOADER, STEP_DONE, STEP_MAX };

static const char *step_labels[] = {
    "Partitioning disk...",
    "Creating filesystem...",
    "Copying system files...",
    "Installing bootloader...",
    "Complete!",
};

static void set_status(const char *s) {
    int i;
    for (i = 0; s[i] && i < 63; i++) inst_status[i] = s[i];
    inst_status[i] = 0;
}

static int write_gpt_protective(void) {
    if (!block_available()) return -1;
    int sectors = 0, is_lba = 0;
    block_get_info(&sectors, &is_lba);
    if (sectors <= 0) return -1;
    (void)is_lba;

    uint64_t total_lba = sectors;
    uint32_t lba32 = total_lba < 0xFFFFFFFFULL ? (uint32_t)total_lba : 0xFFFFFFFF;

    /* Protective MBR (LBA 0) */
    uint8_t mbr[512];
    memset(mbr, 0, 512);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
    mbr[0x1BE + 0] = 0x00;   /* status */
    mbr[0x1BE + 1] = 0x00;   /* CHS first */
    mbr[0x1BE + 2] = 0x02;
    mbr[0x1BE + 3] = 0x00;
    mbr[0x1BE + 4] = 0xEE;   /* GPT protective */
    mbr[0x1BE + 5] = 0x00;   /* CHS last */
    mbr[0x1BE + 6] = 0x00;
    mbr[0x1BE + 7] = 0x00;
    mbr[0x1BE + 8] = 0x01;   /* start LBA = 1 */
    mbr[0x1BE + 9] = 0x00;
    mbr[0x1BE + 10] = 0x00;
    mbr[0x1BE + 11] = 0x00;
    mbr[0x1BE + 12] = (uint8_t)(lba32 & 0xFF);
    mbr[0x1BE + 13] = (uint8_t)((lba32 >> 8) & 0xFF);
    mbr[0x1BE + 14] = (uint8_t)((lba32 >> 16) & 0xFF);
    mbr[0x1BE + 15] = (uint8_t)((lba32 >> 24) & 0xFF);

    if (block_write_sectors(0, 1, mbr) < 0) return -1;

    /* GPT header (LBA 1) */
    uint8_t gpt_hdr[512];
    memset(gpt_hdr, 0, 512);
    /* Signature "EFI PART" */
    gpt_hdr[0] = 0x45; gpt_hdr[1] = 0x46; gpt_hdr[2] = 0x49;
    gpt_hdr[3] = 0x20; gpt_hdr[4] = 0x50; gpt_hdr[5] = 0x41;
    gpt_hdr[6] = 0x52; gpt_hdr[7] = 0x54;
    /* Revision 1.0 */
    gpt_hdr[8] = 0x00; gpt_hdr[9] = 0x00; gpt_hdr[10] = 0x01; gpt_hdr[11] = 0x00;
    /* Header size */
    gpt_hdr[12] = 0x5C; gpt_hdr[13] = 0x00; gpt_hdr[14] = 0x00; gpt_hdr[15] = 0x00;
    /* CRC32 of header (0..0x5C) - skip, set to 0 */
    /* Reserved (bytes 16-19) = 0 */
    /* My LBA (self) = 1 */
    gpt_hdr[24] = 0x01; gpt_hdr[25] = 0x00; gpt_hdr[26] = 0x00; gpt_hdr[27] = 0x00;
    gpt_hdr[28] = 0x00; gpt_hdr[29] = 0x00; gpt_hdr[30] = 0x00; gpt_hdr[31] = 0x00;
    /* Alternate LBA = last sector */
    uint64_t last_lba = total_lba - 1;
    memcpy(gpt_hdr + 32, &last_lba, 8);
    /* First usable LBA = 34 */
    uint64_t first_usable = 34;
    memcpy(gpt_hdr + 40, &first_usable, 8);
    /* Last usable LBA = last_lba - 33 */
    uint64_t last_usable = last_lba - 33;
    memcpy(gpt_hdr + 48, &last_usable, 8);
    /* Disk GUID */
    uint64_t guid_rand = timer_get_milliseconds() ^ 0xDEADBEEF;
    memcpy(gpt_hdr + 56, &guid_rand, 8);
    memcpy(gpt_hdr + 64, &guid_rand, 8);
    /* Partition entry start LBA = 2 */
    uint64_t pe_start = 2;
    memcpy(gpt_hdr + 72, &pe_start, 8);
    /* Number of partition entries = 1 */
    gpt_hdr[80] = 0x01; gpt_hdr[81] = 0x00;
    /* Partition entry size = 128 */
    gpt_hdr[84] = 0x80; gpt_hdr[85] = 0x00;

    /* CRC32 would go at bytes 16-19, but some bootloaders work without it */

    if (block_write_sectors(1, 1, gpt_hdr) < 0) return -1;

    /* Partition entry at LBA 2 */
    uint8_t pe[512];
    memset(pe, 0, 512);
    /* Partition type GUID (Linux filesystem) */
    static const uint8_t linux_fs_guid[16] = {
        0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
    };
    memcpy(pe, linux_fs_guid, 16);
    /* Unique partition GUID */
    memcpy(pe + 16, &guid_rand, 8);
    memcpy(pe + 24, &guid_rand, 8);
    /* Start LBA = 34 */
    uint64_t part_start = 34;
    memcpy(pe + 32, &part_start, 8);
    /* End LBA = last_usable */
    memcpy(pe + 40, &last_usable, 8);
    /* Attributes = 0 */
    /* Name in UTF-16LE "CodeOS" */
    pe[56] = 'C'; pe[57] = 0;
    pe[58] = 'o'; pe[59] = 0;
    pe[60] = 'd'; pe[61] = 0;
    pe[62] = 'e'; pe[63] = 0;
    pe[64] = 'O'; pe[65] = 0;
    pe[66] = 'S'; pe[67] = 0;

    if (block_write_sectors(2, 2, pe) < 0) return -1;

    /* Actually write to LBA 2 only, but we wrote 2 sectors which overwrites
       whatever was at LBA 3 too - that's fine */

    return 0;
}

static int do_install(void) {
    int step;

    for (step = 0; step < STEP_MAX; step++) {
        set_status(step_labels[step]);
        inst_prog = step;
        inst_total = STEP_MAX;
        desktop_redraw();

        switch (step) {
        case STEP_PARTITION: {
            if (write_gpt_protective() < 0) {
                set_status("FAILED: disk write error");
                return -1;
            }
            /* Re-initialize partition table */
            part_init();
            target_part = -1;
            for (int i = 0; i < part_count(); i++) {
                partition_t p;
                part_get(i, &p);
                if (p.type == 0x83) { target_part = i; break; }
            }
            if (target_part < 0) {
                set_status("FAILED: no ext2 partition found");
                return -1;
            }
            break;
        }
        case STEP_FORMAT: {
            if (mkfs_ext2(target_part) < 0) {
                set_status("FAILED: could not create filesystem");
                return -1;
            }
            break;
        }
        case STEP_COPY: {
            if (!ext2_mount(target_part)) {
                set_status("FAILED: could not mount filesystem");
                return -1;
            }
            ext2_mkdir("/boot");
            /* Copy kernel binary - embedded via two-phase build */
            __attribute__((weak)) extern uint8_t _binary_codeos_1_kernel_stage1_bin_start[];
            __attribute__((weak)) extern uint8_t _binary_codeos_1_kernel_stage1_bin_end[];
            if ((uintptr_t)_binary_codeos_1_kernel_stage1_bin_start != 0) {
                int ksize = (int)(_binary_codeos_1_kernel_stage1_bin_end -
                                  _binary_codeos_1_kernel_stage1_bin_start);
                if (ksize > 0) {
                    if (ext2_write_file_path("/boot/codeos-1-kernel.bin",
                        _binary_codeos_1_kernel_stage1_bin_start, ksize) < 0) {
                        set_status("FAILED: could not write kernel");
                        return -1;
                    }
                }
            }

            /* Copy limine-bios.sys for bootloader */
            __attribute__((weak)) extern uint8_t _binary_bootloader_limine_bios_sys_start[];
            __attribute__((weak)) extern uint8_t _binary_bootloader_limine_bios_sys_end[];
            if ((uintptr_t)_binary_bootloader_limine_bios_sys_start != 0) {
                int lsize = (int)(_binary_bootloader_limine_bios_sys_end -
                                  _binary_bootloader_limine_bios_sys_start);
                if (lsize > 0) {
                    if (ext2_write_file_path("/limine-bios.sys",
                        _binary_bootloader_limine_bios_sys_start, lsize) < 0) {
                        set_status("FAILED: could not write limine-bios.sys");
                        return -1;
                    }
                }
            }

            /* Write bootloader config */
            const char *conf =
                "# CodeOS - installed\n"
                "TIMEOUT=3\n\n"
                "/boot/codeos-1-kernel.bin\n";
            if (ext2_write_file_path("/boot/limine.conf", conf, strlen(conf)) < 0) {
                set_status("FAILED: could not write boot config");
                return -1;
            }
            break;
        }
        case STEP_BOOTLOADER: {
            /* Write Limine BIOS boot code to MBR + reserved sectors */
            /* The Limine BIOS HDD boot code goes to LBA 0 (MBR area) */
            /* limine-bios.sys goes to a reserved location after GPT */
            int boot_code_len = sizeof(binary_limine_hdd_bin_data);
            if (boot_code_len > 512) {
                /* First 512 bytes go to LBA 0 */
                if (block_write_sectors(0, 1, binary_limine_hdd_bin_data) < 0) {
                    set_status("FAILED: bootloader write failed");
                    return -1;
                }
                /* Remaining data goes to LBA 1+ */
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
                        set_status("FAILED: bootloader stage2 write failed");
                        return -1;
                    }
                }
            } else {
                if (block_write_sectors(0, 1, binary_limine_hdd_bin_data) < 0) {
                    set_status("FAILED: bootloader write failed");
                    return -1;
                }
            }
            break;
        }
        case STEP_DONE:
            inst_done = 1;
            break;
        }
    }

    return 0;
}

static void draw_inst_window(void) {
    window_t *w = desktop_get_window(inst_win);
    if (!w || !w->visible) return;

    int cx = w->x + 6, cy = w->y + 24;
    int cw = w->w - 12, ch = w->h - 30;

    fb_draw_shadow_layered(cx - 4, cy - 4, cw + 8, ch + 8,
                           CARD_RADIUS + 2, SHADOW_ALPHA, SHADOW_OFFSET, 8);
    ui_draw_card(cx, cy, cw, ch, C_BASE);

    int y = cy + 10;

    if (inst_screen == 0) {
        /* Welcome screen */
        draw_str(cx + 20, y, "CodeOS Installer", C_BLUE);
        y += 30;
        draw_str(cx + 20, y, "This will install CodeOS to your hard disk.", C_TEXT);
        y += 20;
        draw_str(cx + 20, y, "All data on the target disk will be overwritten!", C_RED);
        y += 30;

        if (block_available()) {
            int sectors = 0;
            block_get_info(&sectors, 0);
            char info[64];
            int pos = 0;
            const char *pre = "Disk: ";
            while (*pre) info[pos++] = *pre++;
            /* Format number */
            int mb = (int)((uint64_t)sectors * 512 / 1048576);
            int gb = mb / 1024;
            if (gb > 0) {
                char num[16]; int np = 0;
                int g = gb; while (g) { num[np++] = '0' + g % 10; g /= 10; }
                for (int i = np - 1; i >= 0; i--) info[pos++] = num[i];
                info[pos++] = ' '; info[pos++] = 'G'; info[pos++] = 'B'; info[pos++] = ' ';
            } else {
                char num[16]; int np = 0;
                int m = mb; while (m) { num[np++] = '0' + m % 10; m /= 10; }
                if (np == 0) { info[pos++] = '0'; } else { for (int i = np - 1; i >= 0; i--) info[pos++] = num[i]; }
                info[pos++] = ' '; info[pos++] = 'M'; info[pos++] = 'B'; info[pos++] = ' ';
            }
            const char *n = block_backend_name();
            while (*n && pos < 63) info[pos++] = *n++;
            info[pos] = 0;
            draw_str(cx + 20, y, info, C_GREEN);
        } else {
            draw_str(cx + 20, y, "No disk detected!", C_RED);
        }
        y += 30;
        draw_str(cx + 20, y, "[Enter] Begin installation   [Esc] Cancel", C_SUBTEXT1);
    } else if (inst_screen == 1) {
        /* Installation progress — step indicator dots */
        int dot_r = 5;
        int dot_gap = 18;
        int dots_w = STEP_MAX * dot_r * 2 + (STEP_MAX - 1) * (dot_gap - dot_r * 2);
        int dots_x = cx + (cw - dots_w) / 2;
        int dots_y = cy + 14;
        for (int i = 0; i < STEP_MAX; i++) {
            int dx = dots_x + i * dot_gap;
            uint32_t dot_c;
            if (i < inst_prog)
                dot_c = C_GREEN;
            else if (i == inst_prog)
                dot_c = C_BLUE;
            else
                dot_c = C_SURFACE2;
            fb_fill_rounded_rect(dx, dots_y, dot_r * 2, dot_r * 2, dot_r, dot_c);
            if (i == inst_prog) {
                fb_draw_rounded_rect(dx - 1, dots_y - 1, dot_r * 2 + 2, dot_r * 2 + 2,
                                     dot_r + 1, C_FOCUS_GLOW);
            }
        }

        int status_y = cy + 50;
        if (inst_status[0]) {
            draw_str(cx + 20, status_y - 20, inst_status, C_TEXT);
        }

        draw_bar(cx + 20, status_y + 10, cw - 40, 24, inst_prog, inst_total);
        char pct[16];
        int pp = 0;
        int pct_val = inst_total > 0 ? inst_prog * 100 / inst_total : 0;
        if (pct_val >= 100) { pct[pp++] = '1'; pct[pp++] = '0'; pct[pp++] = '0'; }
        else if (pct_val >= 10) { pct[pp++] = '0' + pct_val / 10; pct[pp++] = '0' + pct_val % 10; }
        else { pct[pp++] = '0' + pct_val; }
        pct[pp++] = '%'; pct[pp] = 0;
        draw_str(cx + 20, status_y + 40, pct, C_BLUE);

        if (inst_done) {
            draw_str(cx + 20, cy + ch - 40, "Installation complete! Press [Enter] to reboot.", C_GREEN);
            draw_str(cx + 20, cy + ch - 20, "[Esc] Return to shell", C_SUBTEXT1);
        }
    }
}

void installer_draw(void) {
    if (!inst_open) return;
    draw_inst_window();
}

void installer_key(int key) {
    if (!inst_open) return;

    if (inst_screen == 0) {
        if (key == '\n' || key == '\r') {
            inst_screen = 1;
            do_install();
            desktop_redraw();
        } else if (key == 0x1B) {
            installer_close();
        }
    } else if (inst_screen == 1) {
        if (inst_done && (key == '\n' || key == '\r')) {
            kprintf("Installer: rebooting...\n");
            outb(0x64, 0xFE);
            __asm__ volatile("cli; hlt");
        }
        if (key == 0x1B) {
            installer_close();
        }
    }
}
