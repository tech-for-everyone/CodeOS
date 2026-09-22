#include "ext2.h"
#include "part.h"
#include "string.h"
#include "kprintf.h"
#include "block.h"
#include "mm.h"
#include "pmm.h"
#include "vmm.h"

#define EXT2_SB_OFFSET 1024
#define EXT2_BLOCK_SIZE(sb) (1024 << (sb).log_block_size)
#define EXT2_FRAG_SIZE(sb)  (1024 << (sb).log_frag_size)

struct ext2_superblock {
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
    char     last_mounted[64];
    uint32_t algo_bitmap;
    uint8_t  prealloc_blocks;
    uint8_t  prealloc_dir_blocks;
    uint16_t padding;
    uint8_t  journal_uuid[16];
    uint32_t journal_inum;
    uint32_t journal_dev;
    uint32_t last_orphan;
    uint32_t hash_seed[4];
    uint8_t  def_hash_version;
    uint8_t  jnl_backup_type;
    uint16_t desc_size;
    uint32_t default_mount_opts;
    uint32_t first_meta_bg;
    uint32_t mkfs_time;
    uint32_t jnl_blocks[17];
} __attribute__((packed));

struct ext2_bg_desc {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint32_t reserved[3];
} __attribute__((packed));

#define EXT2_MAGIC      0xEF53
#define EXT2_INODE_BLOCKS 15
#define EXT2_NDIR_BLOCKS 12

static int ext2_mounted_val;
static struct ext2_superblock sb;
static int sb_part_idx;
static uint32_t block_size;
static uint32_t inodes_per_group;
static uint32_t blocks_per_group;
static uint32_t bg_desc_blocks;
static uint32_t inode_size;

static uint8_t *block_buf;
static size_t block_buf_size;

static int read_block(uint32_t block_num) {
    uint32_t sector = (uint64_t)block_num * block_size / BLOCK_SECTOR_SIZE;
    uint32_t count = block_size / BLOCK_SECTOR_SIZE;
    if (count > 255) count = 255;
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    return block_read_sectors(p.start_lba + sector, (uint8_t)count, block_buf);
}

int ext2_mount(int part_idx) {
    partition_t p;
    if (part_get(part_idx, &p) < 0) return 0;
    if (p.type != 0x83) return 0;

    sb_part_idx = part_idx;
    uint32_t sb_sector = p.start_lba + (EXT2_SB_OFFSET / BLOCK_SECTOR_SIZE);
    uint8_t buf[2 * BLOCK_SECTOR_SIZE];
    if (block_read_sectors(sb_sector, 2, buf) < 0) return 0;
    memcpy(&sb, buf + (EXT2_SB_OFFSET % BLOCK_SECTOR_SIZE), sizeof(struct ext2_superblock));

    if (sb.magic != EXT2_MAGIC) return 0;

    block_size = EXT2_BLOCK_SIZE(sb);

    /* Allocate block buffer large enough for the actual block size */
    if (block_buf) {
        pmm_free_pages(virt_to_phys((uint64_t)block_buf),
                       (block_buf_size + 0xFFF) / 0x1000);
    }
    size_t buf_pages = (block_size + 0xFFF) / 0x1000;
    uint64_t buf_phys = pmm_alloc_pages(buf_pages);
    if (!buf_phys) return 0;
    block_buf = (uint8_t *)phys_to_virt(buf_phys);
    block_buf_size = block_size;

    inodes_per_group = sb.inodes_per_group;
    blocks_per_group = sb.blocks_per_group;
    inode_size = sb.inode_size ? sb.inode_size : 128;

    uint32_t bg_count = (sb.blocks_count + blocks_per_group - 1) / blocks_per_group;
    bg_desc_blocks = (bg_count * sizeof(struct ext2_bg_desc) + block_size - 1) / block_size;

    ext2_mounted_val = 1;
    return 1;
}

int ext2_mounted(void) {
    return ext2_mounted_val;
}

static int read_bg_desc(int bg, struct ext2_bg_desc *bgd) {
    uint32_t bg_desc_block = (sb.first_data_block + 1);
    uint32_t block_num = bg_desc_block + (bg * sizeof(struct ext2_bg_desc)) / block_size;
    if (read_block(block_num) < 0) return -1;
    uint32_t off = (bg * sizeof(struct ext2_bg_desc)) % block_size;
    memcpy(bgd, block_buf + off, sizeof(struct ext2_bg_desc));
    return 0;
}

int ext2_read_inode(int inode_num, void *buf) {
    int bg = (inode_num - 1) / inodes_per_group;
    int idx = (inode_num - 1) % inodes_per_group;
    struct ext2_bg_desc bgd;
    if (read_bg_desc(bg, &bgd) < 0) return -1;
    uint32_t tbl_block = bgd.inode_table;
    uint32_t byte_off = idx * inode_size;
    if (block_size == 0) return -1;
    uint32_t block_num = tbl_block + byte_off / block_size;
    if (read_block(block_num) < 0) return -1;
    memcpy(buf, block_buf + (byte_off % block_size), inode_size > 128 ? 128 : inode_size);
    return 0;
}

static uint32_t read_inode_block(struct ext2_inode *inode, int block_idx) {
    if (block_idx < 0) return 0;
    uint32_t uidx = (uint32_t)block_idx;
    if (uidx < 12) return inode->block[uidx];

    uint32_t ptrs_per_block = block_size / 4;
    if (uidx < 12 + ptrs_per_block) {
        if (read_block(inode->block[12]) < 0) return 0;
        return ((uint32_t*)block_buf)[uidx - 12];
    }

    uint32_t indirects = ptrs_per_block * ptrs_per_block;
    if (uidx < 12 + ptrs_per_block + indirects) {
        if (read_block(inode->block[13]) < 0) return 0;
        uint32_t *indir = (uint32_t*)block_buf;
        uint32_t i_idx = uidx - 12 - ptrs_per_block;
        if (read_block(indir[i_idx / ptrs_per_block]) < 0) return 0;
        return ((uint32_t*)block_buf)[i_idx % ptrs_per_block];
    }

    return 0;
}

int ext2_read_file(int inode_num, void *buf, int max, int offset) {
    struct ext2_inode inode;
    if (ext2_read_inode(inode_num, &inode) < 0) return -1;

    int file_size = inode.size;
    if (offset >= file_size) return 0;
    if (max <= 0) return file_size - offset;
    if (offset + max > file_size) max = file_size - offset;

    int total = 0;
    int pos = offset;
    while (pos < file_size && total < max) {
        int block_idx = pos / block_size;
        int block_off = pos % block_size;
        uint32_t phys_block = read_inode_block(&inode, block_idx);
        int copy = block_size - block_off;
        if (copy > max - total) copy = max - total;
        if (copy > file_size - pos) copy = file_size - pos;
        if (phys_block == 0) {
            memset((uint8_t*)buf + total, 0, copy);
        } else {
            if (read_block(phys_block) < 0) break;
            memcpy((uint8_t*)buf + total, block_buf + block_off, copy);
        }
        total += copy;
        pos += copy;
    }
    return total;
}

int ext2_list_root(void) {
    return ext2_find("/", NULL);
}

/* Read symlink target from an inode. Returns 0 on success, -1 on error. */
static int read_symlink_target(int inode_num, struct ext2_inode *inode, char *target, int max_len) {
    if (!(inode->mode & EXT2_S_IFLNK)) return -1;
    int target_len = inode->size;
    if (target_len <= 0 || target_len >= max_len) return -1;

    if (target_len <= 60) {
        memcpy(target, (char *)inode->block, target_len);
    } else {
        if (ext2_read_file(inode_num, target, target_len, 0) < 0) return -1;
    }
    target[target_len] = 0;
    return 0;
}

int ext2_find(const char *path, ext2_dirent_t *ent) {
    int current_inode = 2;
    struct ext2_inode inode;
    int symlink_depth = 0;
    #define MAX_SYMLINK_DEPTH 8

    if (!ext2_mounted_val) return -1;

    if (!path || *path == 0) path = "/";
    while (*path == '/') path++;

    if (*path == 0) {
        if (ext2_read_inode(2, &inode) < 0) return -1;
        if (ent) {
            ent->inode = 2;
            ent->is_dir = 1;
            ent->size = inode.size;
            ent->valid = 1;
            strcpy(ent->name, "/");
        }
        return 0;
    }

    char component[EXT2_NAME_MAX];
    const char *p = path;
    while (1) {
        int ci = 0;
        while (*p && *p != '/' && ci < EXT2_NAME_MAX - 1)
            component[ci++] = *p++;
        component[ci] = 0;
        while (*p == '/') p++;

        if (ext2_read_inode(current_inode, &inode) < 0) return -1;

        /* Follow symlinks in path components */
        int follow_limit = 0;
        while ((inode.mode & EXT2_S_IFMT) == EXT2_S_IFLNK) {
            if (++follow_limit > MAX_SYMLINK_DEPTH) return -1;
            if (symlink_depth++ > MAX_SYMLINK_DEPTH) return -1;

            char link_target[EXT2_NAME_MAX];
            if (read_symlink_target(current_inode, &inode, link_target, sizeof(link_target)) < 0)
                return -1;

            /* Build new path: link_target + remaining path components */
            char new_path[EXT2_NAME_MAX * 2];
            int np = 0;
            for (int i = 0; link_target[i] && np < (int)sizeof(new_path) - 2; i++)
                new_path[np++] = link_target[i];
            if (*p && np < (int)sizeof(new_path) - 2) {
                new_path[np++] = '/';
                for (const char *s = p; *s && np < (int)sizeof(new_path) - 2; s++)
                    new_path[np++] = *s;
            }
            new_path[np] = 0;

            if (new_path[0] == '/') {
                p = new_path + 1;
                current_inode = 2;
            } else {
                p = new_path;
            }
            while (*p == '/') p++;
            if (*p == 0) {
                if (ext2_read_inode(current_inode, &inode) < 0) return -1;
                break;
            }

            ci = 0;
            while (*p && *p != '/' && ci < EXT2_NAME_MAX - 1)
                component[ci++] = *p++;
            component[ci] = 0;
            while (*p == '/') p++;
            if (ext2_read_inode(current_inode, &inode) < 0) return -1;
        }

        if (!(inode.mode & EXT2_S_IFDIR)) return -1;

        int dir_size = inode.size;
        uint8_t *dir_buf = (uint8_t *)malloc(dir_size ? dir_size : 1);
        if (!dir_buf) return -1;
        if (dir_size > 0 && ext2_read_file(current_inode, dir_buf, dir_size, 0) < 0) {
            free(dir_buf);
            return -1;
        }

        int found = 0;
        int off = 0;
        while (off < dir_size) {
            struct ext2_dirent *de = (struct ext2_dirent*)(dir_buf + off);
            if (de->inode == 0) { off += de->rec_len; continue; }
            if (de->rec_len == 0) break;

            char de_name[EXT2_NAME_MAX];
            int nl = de->name_len < EXT2_NAME_MAX - 1 ? de->name_len : EXT2_NAME_MAX - 1;
            memcpy(de_name, de->name, nl);
            de_name[nl] = 0;

            int match = (strcmp(de_name, component) == 0);
            if (*p == 0 && match) {
                if (ent) {
                    struct ext2_inode fi;
                    ent->inode = de->inode;
                    ent->is_dir = (de->file_type == EXT2_FT_DIR);
                    strcpy(ent->name, de_name);
                    ent->valid = 1;
                    if (ext2_read_inode(de->inode, &fi) == 0) {
                        ent->size = fi.size;
                        int fmt = fi.mode & EXT2_S_IFMT;
                        ent->is_dir = (fmt == EXT2_S_IFDIR);
                    }
                }
                free(dir_buf);
                return 0;
            }

            if (match && *p) {
                current_inode = de->inode;
                found = 1;
                free(dir_buf);
                break;
            }
            off += de->rec_len;
        }
        free(dir_buf);
        if (!found) return -1;
    }
}

int ext2_list_dir(const char *path) {
    ext2_dirent_t self;
    if (ext2_find(path, &self) < 0 || !self.valid) return -1;
    if (!self.is_dir) { kprintf("%s  (%d bytes)\n", self.name, self.size); return 0; }

    struct ext2_inode inode;
    if (ext2_read_inode(self.inode, &inode) < 0) return -1;
    int dir_size = inode.size;
    uint8_t *dir_buf = (uint8_t *)malloc(dir_size ? dir_size : 1);
    if (!dir_buf) return -1;
    if (ext2_read_file(self.inode, dir_buf, dir_size, 0) < 0) {
        free(dir_buf);
        return -1;
    }

    int off = 0;
    while (off < dir_size) {
        struct ext2_dirent *de = (struct ext2_dirent*)(dir_buf + off);
        if (de->inode == 0 || de->rec_len == 0) { off += de->rec_len ? de->rec_len : 1; continue; }
        char name[256];
        int nl = de->name_len < 255 ? de->name_len : 255;
        memcpy(name, de->name, nl); name[nl] = 0;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) { off += de->rec_len; continue; }
        kprintf("  %c  %s\n", de->file_type == EXT2_FT_DIR ? 'd' : 'f', name);
        off += de->rec_len;
    }
    free(dir_buf);
    return 0;
}

int ext2_read_file_path(const char *path, void *buf, int max) {
    ext2_dirent_t ent;
    if (ext2_find(path, &ent) < 0 || !ent.valid) return -1;
    if (ent.is_dir) return -1;
    return ext2_read_file(ent.inode, buf, max, 0);
}

/* --- write support --- */

static int write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    return block_write_sectors(p.start_lba + lba, (uint8_t)count, buf);
}



static int write_block(uint32_t block_num) {
    uint32_t sector = (uint64_t)block_num * block_size / BLOCK_SECTOR_SIZE;
    uint32_t count = block_size / BLOCK_SECTOR_SIZE;
    return write_sectors(sector, count, block_buf);
}

static int write_superblock(void) {
    uint32_t sb_sector = EXT2_SB_OFFSET / BLOCK_SECTOR_SIZE;
    uint8_t tmp[BLOCK_SECTOR_SIZE];
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp + (EXT2_SB_OFFSET % BLOCK_SECTOR_SIZE), &sb, sizeof(struct ext2_superblock));
    return write_sectors(sb_sector, 1, tmp);
}

static int write_bg_desc(int bg, struct ext2_bg_desc *bgd) {
    uint32_t bg_desc_block = sb.first_data_block + 1;
    uint32_t block_num = bg_desc_block + (bg * sizeof(struct ext2_bg_desc)) / block_size;
    uint32_t off = (bg * sizeof(struct ext2_bg_desc)) % block_size;
    if (read_block(block_num) < 0) return -1;
    memcpy(block_buf + off, bgd, sizeof(struct ext2_bg_desc));
    return write_block(block_num);
}

int ext2_write_inode(int inode_num, void *buf) {
    int bg = (inode_num - 1) / inodes_per_group;
    int idx = (inode_num - 1) % inodes_per_group;
    struct ext2_bg_desc bgd;
    if (read_bg_desc(bg, &bgd) < 0) return -1;
    uint32_t tbl_block = bgd.inode_table;
    uint32_t byte_off = idx * inode_size;
    uint32_t block_num = tbl_block + byte_off / block_size;
    if (read_block(block_num) < 0) return -1;
    memcpy(block_buf + (byte_off % block_size), buf, inode_size > 128 ? 128 : inode_size);
    return write_block(block_num);
}

static void set_block_bit(int bg, uint32_t bit, int used) {
    struct ext2_bg_desc bgd;
    read_bg_desc(bg, &bgd);
    uint32_t bitmap_block = bgd.block_bitmap;
    uint32_t byte_off = bit / 8;
    uint32_t bit_off = bit % 8;
    uint32_t block_num = bitmap_block + byte_off / block_size;
    read_block(block_num);
    if (used)
        block_buf[byte_off % block_size] |= (1 << bit_off);
    else
        block_buf[byte_off % block_size] &= ~(1 << bit_off);
    write_block(block_num);
}

static uint32_t alloc_block_goal(int preferred_bg) {
    uint32_t bg_count = (sb.blocks_count + blocks_per_group - 1) / blocks_per_group;
    if (preferred_bg < 0 || (uint32_t)preferred_bg >= bg_count) preferred_bg = 0;

    for (uint32_t scan = 0; scan < bg_count; scan++) {
        uint32_t bg = (preferred_bg + scan) % bg_count;
        struct ext2_bg_desc bgd;
        if (read_bg_desc(bg, &bgd) < 0) continue;
        if (bgd.free_blocks_count == 0) continue;

        uint32_t blocks_in_bg = blocks_per_group;
        if (bg == bg_count - 1) {
            uint32_t last_bg_start = bg * blocks_per_group;
            if (last_bg_start + blocks_in_bg > sb.blocks_count)
                blocks_in_bg = sb.blocks_count - last_bg_start;
        }

        uint32_t bitmap_block = bgd.block_bitmap;
        for (uint32_t byte_idx = 0; byte_idx < block_size && byte_idx * 8 < blocks_in_bg; byte_idx++) {
            uint32_t read_block_num = bitmap_block + byte_idx / block_size;
            if (read_block(read_block_num) < 0) break;

            if (block_buf[byte_idx % block_size] == 0xFF) continue;

            for (int bit = 0; bit < 8 && (int)(byte_idx * 8 + bit) < (int)blocks_in_bg; bit++) {
                if (!(block_buf[byte_idx % block_size] & (1 << bit))) {
                    uint32_t abs_block = bg * blocks_per_group + byte_idx * 8 + bit;
                    if (abs_block >= sb.blocks_count) continue;

                    set_block_bit(bg, byte_idx * 8 + bit, 1);

                    bgd.free_blocks_count--;
                    write_bg_desc(bg, &bgd);

                    sb.free_blocks_count--;
                    write_superblock();

                    memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);
                    write_block(abs_block);

                    return abs_block;
                }
            }
        }
    }
    return 0;
}

static uint32_t alloc_block_for_inode(int inode_num) {
    if (inode_num <= 0) return alloc_block_goal(-1);
    int preferred_bg = (inode_num - 1) / inodes_per_group;
    return alloc_block_goal(preferred_bg);
}

static void free_block(uint32_t phys_block) {
    if (phys_block == 0) return;
    uint32_t bg = phys_block / blocks_per_group;
    struct ext2_bg_desc bgd;
    read_bg_desc(bg, &bgd);
    uint32_t bit = phys_block - bg * blocks_per_group;
    set_block_bit(bg, bit, 0);
    bgd.free_blocks_count++;
    write_bg_desc(bg, &bgd);
    sb.free_blocks_count++;
    write_superblock();
}

static void free_blocks_by_inode(struct ext2_inode *inode) {
    uint32_t ptrs_per_block = block_size / 4;

    for (int i = 0; i < 12; i++) {
        if (inode->block[i] == 0) continue;
        free_block(inode->block[i]);
        inode->block[i] = 0;
    }

    if (inode->block[12]) {
        if (read_block(inode->block[12]) == 0) {
            uint32_t *indir = (uint32_t *)block_buf;
            for (uint32_t i = 0; i < ptrs_per_block; i++) {
                if (indir[i] == 0) continue;
                free_block(indir[i]);
            }
        }
        free_block(inode->block[12]);
        inode->block[12] = 0;
    }

    if (inode->block[13]) {
        if (read_block(inode->block[13]) == 0) {
            uint32_t *dindir = (uint32_t *)block_buf;
            for (uint32_t i = 0; i < ptrs_per_block; i++) {
                if (dindir[i] == 0) continue;
                if (read_block(dindir[i]) == 0) {
                    uint32_t *indir = (uint32_t *)block_buf;
                    for (uint32_t j = 0; j < ptrs_per_block; j++) {
                        if (indir[j] == 0) continue;
                        free_block(indir[j]);
                    }
                }
                free_block(dindir[i]);
            }
        }
        free_block(inode->block[13]);
        inode->block[13] = 0;
    }
}

int ext2_truncate(int inode_num) {
    struct ext2_inode inode;
    if (ext2_read_inode(inode_num, &inode) < 0) return -1;
    free_blocks_by_inode(&inode);
    inode.size = 0;
    inode.blocks = 0;
    return ext2_write_inode(inode_num, &inode);
}

static int write_indir_ptr(uint32_t indir_block, uint32_t index, uint32_t ptr) {
    if (indir_block == 0) return -1;
    if (read_block(indir_block) < 0) return -1;
    ((uint32_t *)block_buf)[index] = ptr;
    return write_block(indir_block);
}

static uint32_t alloc_or_get_indir_block(uint32_t parent_block, uint32_t parent_index, int inode_num) {
    uint32_t blk;
    if (parent_block == 0) {
        blk = alloc_block_for_inode(inode_num);
        if (blk == 0) return 0;
        memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);
        write_block(blk);
        return blk;
    }
    if (read_block(parent_block) < 0) return 0;
    blk = ((uint32_t *)block_buf)[parent_index];
    if (blk != 0) return blk;
    blk = alloc_block_for_inode(inode_num);
    if (blk == 0) return 0;
    memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);
    write_block(blk);
    write_indir_ptr(parent_block, parent_index, blk);
    return blk;
}

int ext2_write_file(int inode_num, const void *buf, int max, int offset) {
    struct ext2_inode inode;
    if (ext2_read_inode(inode_num, &inode) < 0) return -1;

    int new_size = offset + max;
    int total = 0;
    int pos = offset;

    while (pos < new_size) {
        int block_idx = pos / block_size;
        int block_off = pos % block_size;
        int copy = block_size - block_off;
        if (copy > max - total) copy = max - total;

        uint32_t phys_block = read_inode_block(&inode, block_idx);
        if (phys_block == 0) {
            phys_block = alloc_block_for_inode(inode_num);
            if (phys_block == 0) return -1;

            if (block_idx < 12) {
                inode.block[block_idx] = phys_block;
            } else {
                uint32_t ptrs_per_block = block_size / 4;
                uint32_t ind_idx = (uint32_t)block_idx - 12;

                if (ind_idx < ptrs_per_block) {
                    if (inode.block[12] == 0) {
                        inode.block[12] = alloc_block_for_inode(inode_num);
                        if (inode.block[12] == 0) return -1;
                        memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);
                        write_block(inode.block[12]);
                    }
                    write_indir_ptr(inode.block[12], ind_idx, phys_block);
                } else {
                    uint32_t dind_idx = (ind_idx - ptrs_per_block) / ptrs_per_block;
                    uint32_t ind_idx2 = (ind_idx - ptrs_per_block) % ptrs_per_block;
                    if (inode.block[13] == 0) {
                        inode.block[13] = alloc_block_for_inode(inode_num);
                        if (inode.block[13] == 0) return -1;
                        memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);
                        write_block(inode.block[13]);
                    }
                    uint32_t ind_block = alloc_or_get_indir_block(inode.block[13], dind_idx, inode_num);
                    if (ind_block == 0) return -1;
                    write_indir_ptr(ind_block, ind_idx2, phys_block);
                }
            }

            inode.blocks += block_size / 512;
        }

        memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);
        memcpy(block_buf + block_off, (const uint8_t *)buf + total, copy);
        write_block(phys_block);

        total += copy;
        pos += copy;
    }

    if (new_size > (int)inode.size) inode.size = new_size;
    ext2_write_inode(inode_num, &inode);
    return total;
}

int ext2_write_file_path(const char *path, const void *buf, int max) {
    ext2_dirent_t ent;
    if (ext2_find(path, &ent) < 0 || !ent.valid) return -1;
    if (ent.is_dir) return -1;
    ext2_truncate(ent.inode);
    return ext2_write_file(ent.inode, buf, max, 0);
}

/* ── Inode allocation ── */

static int alloc_inode(void) {
    uint32_t bg_count = (sb.blocks_count + blocks_per_group - 1) / blocks_per_group;
    for (uint32_t bg = 0; bg < bg_count; bg++) {
        struct ext2_bg_desc bgd;
        if (read_bg_desc(bg, &bgd) < 0) continue;
        if (bgd.free_inodes_count == 0) continue;

        uint32_t bitmap_block = bgd.inode_bitmap;
        uint32_t inodes_in_bg = inodes_per_group;
        if (bg == bg_count - 1) {
            uint32_t total_inodes = inodes_per_group * bg_count;
            if (sb.inodes_count < total_inodes)
                inodes_in_bg = sb.inodes_count - bg * inodes_per_group;
        }

        for (uint32_t byte_idx = 0; byte_idx < block_size && byte_idx * 8 < inodes_in_bg; byte_idx++) {
            uint32_t read_block_num = bitmap_block + byte_idx / block_size;
            if (read_block(read_block_num) < 0) break;

            if (block_buf[byte_idx % block_size] == 0xFF) continue;

            for (int bit = 0; bit < 8 && (int)(byte_idx * 8 + bit) < (int)inodes_in_bg; bit++) {
                if (!(block_buf[byte_idx % block_size] & (1 << bit))) {
                    int inode_num = bg * inodes_per_group + byte_idx * 8 + bit + 1;
                    if (inode_num > (int)sb.inodes_count) continue;

                    /* Mark inode bit as used */
                    block_buf[byte_idx % block_size] |= (1 << bit);
                    write_block(read_block_num);

                    bgd.free_inodes_count--;
                    write_bg_desc(bg, &bgd);

                    sb.free_inodes_count--;
                    write_superblock();

                    /* Zero out the inode */
                    struct ext2_inode zero_inode;
                    memset(&zero_inode, 0, sizeof(zero_inode));
                    ext2_write_inode(inode_num, &zero_inode);

                    return inode_num;
                }
            }
        }
    }
    return -1;
}

static void free_inode(int inode_num) {
    if (inode_num <= 0) return;
    uint32_t bg = (inode_num - 1) / inodes_per_group;
    uint32_t idx = (inode_num - 1) % inodes_per_group;
    uint32_t bit = idx;

    struct ext2_bg_desc bgd;
    read_bg_desc(bg, &bgd);
    uint32_t bitmap_block = bgd.inode_bitmap;
    uint32_t byte_off = bit / 8;
    uint32_t bit_off = bit % 8;
    uint32_t block_num = bitmap_block + byte_off / block_size;
    read_block(block_num);
    block_buf[byte_off % block_size] &= ~(1 << bit_off);
    write_block(block_num);

    bgd.free_inodes_count++;
    write_bg_desc(bg, &bgd);

    sb.free_inodes_count++;
    write_superblock();
}

/* ── Directory entry manipulation ── */

static int add_dirent(int dir_inode, const char *name, int new_inode, int file_type) {
    struct ext2_inode dir_inode_data;
    if (ext2_read_inode(dir_inode, &dir_inode_data) < 0) return -1;

    int name_len = strlen(name);
    if (name_len <= 0 || name_len > EXT2_NAME_MAX - 1) return -1;

    int entry_size = sizeof(struct ext2_dirent) + name_len;
    entry_size = (entry_size + 3) & ~3;
    if (entry_size < 8) entry_size = 8;

    int dir_size = dir_inode_data.size;
    uint8_t *dir_buf = (uint8_t *)malloc((dir_size ? dir_size : 1) + entry_size + block_size);
    if (!dir_buf) return -1;
    if (dir_size > 0) {
        if (ext2_read_file(dir_inode, dir_buf, dir_size, 0) < 0) {
            free(dir_buf);
            return -1;
        }
    }

    /* Look for empty space in existing entries */
    int off = 0;
    while (off < dir_size) {
        struct ext2_dirent *de = (struct ext2_dirent *)(dir_buf + off);
        if (de->inode == 0) {
            if (de->rec_len >= entry_size) {
                de->inode = new_inode;
                de->name_len = name_len;
                de->file_type = file_type;
                memcpy(de->name, name, name_len);
                int r = ext2_write_file(dir_inode, dir_buf, dir_size, 0);
                free(dir_buf);
                return r;
            }
        }
        if (de->rec_len == 0) break;
        off += de->rec_len;
    }

    /* Need to extend the directory */
    if (dir_size > 0) {
        int last_off = 0;
        int last_rec_len = 0;
        off = 0;
        while (off < dir_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(dir_buf + off);
            if (de->inode == 0) { off += de->rec_len; continue; }
            if (de->rec_len == 0) break;
            last_off = off;
            last_rec_len = de->rec_len;
            off += de->rec_len;
        }
        int free_space = last_rec_len - (sizeof(struct ext2_dirent) + ((struct ext2_dirent *)(dir_buf + last_off))->name_len);
        free_space = (free_space + 3) & ~3;
        if (free_space >= entry_size) {
            struct ext2_dirent *last = (struct ext2_dirent *)(dir_buf + last_off);
            int old_rec = last->rec_len;
            last->rec_len = old_rec - (last_off + old_rec - off);
            off = last_off + last->rec_len;
            struct ext2_dirent *new_de = (struct ext2_dirent *)(dir_buf + off);
            new_de->inode = new_inode;
            new_de->rec_len = old_rec - (off - last_off);
            new_de->name_len = name_len;
            new_de->file_type = file_type;
            memcpy(new_de->name, name, name_len);
            int r = ext2_write_file(dir_inode, dir_buf, dir_size, 0);
            free(dir_buf);
            return r;
        }
    }

    /* Append new block */
    int new_size = dir_size + entry_size;
    uint8_t *tmp = (uint8_t *)malloc(new_size);
    if (!tmp) { free(dir_buf); return -1; }
    if (dir_size > 0) memcpy(tmp, dir_buf, dir_size);
    struct ext2_dirent *new_de = (struct ext2_dirent *)(tmp + dir_size);
    new_de->inode = new_inode;
    new_de->rec_len = entry_size;
    new_de->name_len = name_len;
    new_de->file_type = file_type;
    memcpy(new_de->name, name, name_len);
    int r = ext2_write_file(dir_inode, tmp, new_size, 0);
    free(tmp);
    free(dir_buf);
    return r;
}

static int del_dirent(int dir_inode, const char *name) {
    struct ext2_inode dir_inode_data;
    if (ext2_read_inode(dir_inode, &dir_inode_data) < 0) return -1;

    int dir_size = dir_inode_data.size;
    if (dir_size <= 0) return -1;
    uint8_t *dir_buf = (uint8_t *)malloc(dir_size);
    if (!dir_buf) return -1;
    if (ext2_read_file(dir_inode, dir_buf, dir_size, 0) < 0) {
        free(dir_buf);
        return -1;
    }

    int off = 0;
    while (off < dir_size) {
        struct ext2_dirent *de = (struct ext2_dirent *)(dir_buf + off);
        if (de->inode == 0) { off += de->rec_len; continue; }
        if (de->rec_len == 0) break;
        char de_name[EXT2_NAME_MAX];
        int nl = de->name_len < EXT2_NAME_MAX - 1 ? de->name_len : EXT2_NAME_MAX - 1;
        memcpy(de_name, de->name, nl);
        de_name[nl] = 0;
        if (strcmp(de_name, name) == 0) {
            de->inode = 0;
            int r = ext2_write_file(dir_inode, dir_buf, dir_size, 0);
            free(dir_buf);
            return r;
        }
        off += de->rec_len;
    }
    free(dir_buf);
    return -1;
}

/* ── Directory operations ── */

int ext2_mkdir(const char *path) {
    char dir_part[EXT2_NAME_MAX], name_part[EXT2_NAME_MAX];
    const char *p = path;
    while (*p == '/') p++;
    if (*p == 0) return -1;

    /* Find parent directory */
    char tmp[EXT2_NAME_MAX];
    int i;
    for (i = 0; path[i] && i < EXT2_NAME_MAX - 1; i++) tmp[i] = path[i];
    tmp[i] = 0;

    /* Find last '/' */
    int last_slash = -1;
    for (i = 0; tmp[i]; i++) if (tmp[i] == '/') last_slash = i;

    if (last_slash < 0) {
        strcpy(dir_part, "/");
        strcpy(name_part, tmp);
    } else {
        int j;
        for (j = 0; j < last_slash && j < EXT2_NAME_MAX - 1; j++) dir_part[j] = tmp[j];
        dir_part[j] = 0;
        if (dir_part[0] == 0) { dir_part[0] = '/'; dir_part[1] = 0; }
        int ni = 0;
        for (i = last_slash + 1; tmp[i] && ni < EXT2_NAME_MAX - 1; i++)
            name_part[ni++] = tmp[i];
        name_part[ni] = 0;
    }

    ext2_dirent_t parent_ent;
    if (ext2_find(dir_part, &parent_ent) < 0 || !parent_ent.valid || !parent_ent.is_dir)
        return -1;

    /* Check if already exists */
    ext2_dirent_t existing;
    if (ext2_find(path, &existing) >= 0 && existing.valid)
        return -1; /* Already exists */

    int inode_num = alloc_inode();
    if (inode_num < 0) return -1;

    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFDIR | 0x1FF; /* drwxrwxrwx */
    inode.uid = 0;
    inode.gid = 0;
    inode.size = 0;
    inode.links_count = 2;
    inode.blocks = 0;
    inode.mtime = 0;
    if (ext2_write_inode(inode_num, &inode) < 0) {
        free_inode(inode_num);
        return -1;
    }

    if (add_dirent(parent_ent.inode, name_part, inode_num, EXT2_FT_DIR) < 0) {
        free_inode(inode_num);
        return -1;
    }

    /* Add . and .. entries */
    /* Need a data block for the directory */
    uint32_t block = alloc_block_for_inode(inode_num);
    if (block == 0) return -1;
    memset(block_buf, 0, block_size > sizeof(block_buf) ? sizeof(block_buf) : block_size);

    struct ext2_dirent *dot = (struct ext2_dirent *)block_buf;
    dot->inode = inode_num;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';

    struct ext2_dirent *dotdot = (struct ext2_dirent *)(block_buf + 12);
    dotdot->inode = parent_ent.inode;
    dotdot->rec_len = block_size - 12;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    write_block(block);

    inode.block[0] = block;
    inode.size = block_size;
    inode.blocks = block_size / 512;
    ext2_write_inode(inode_num, &inode);

    return 0;
}

int ext2_creat(const char *path) {
    char dir_part[EXT2_NAME_MAX], name_part[EXT2_NAME_MAX];

    char tmp[EXT2_NAME_MAX];
    int i;
    for (i = 0; path[i] && i < EXT2_NAME_MAX - 1; i++) tmp[i] = path[i];
    tmp[i] = 0;

    int last_slash = -1;
    for (i = 0; tmp[i]; i++) if (tmp[i] == '/') last_slash = i;

    if (last_slash < 0) {
        strcpy(dir_part, "/");
        strcpy(name_part, tmp);
    } else {
        int j;
        for (j = 0; j < last_slash && j < EXT2_NAME_MAX - 1; j++) dir_part[j] = tmp[j];
        dir_part[j] = 0;
        if (dir_part[0] == 0) { dir_part[0] = '/'; dir_part[1] = 0; }
        int ni = 0;
        for (i = last_slash + 1; tmp[i] && ni < EXT2_NAME_MAX - 1; i++)
            name_part[ni++] = tmp[i];
        name_part[ni] = 0;
    }

    ext2_dirent_t parent_ent;
    if (ext2_find(dir_part, &parent_ent) < 0 || !parent_ent.valid || !parent_ent.is_dir)
        return -1;

    ext2_dirent_t existing;
    if (ext2_find(path, &existing) >= 0 && existing.valid)
        return -1;

    int inode_num = alloc_inode();
    if (inode_num < 0) return -1;

    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFREG | 0x1A4; /* -rw-r----- */
    inode.uid = 0;
    inode.gid = 0;
    inode.size = 0;
    inode.links_count = 1;
    inode.blocks = 0;
    if (ext2_write_inode(inode_num, &inode) < 0) {
        free_inode(inode_num);
        return -1;
    }

    if (add_dirent(parent_ent.inode, name_part, inode_num, EXT2_FT_REG) < 0) {
        free_inode(inode_num);
        return -1;
    }

    return inode_num;
}

int ext2_unlink(const char *path) {
    ext2_dirent_t ent;
    if (ext2_find(path, &ent) < 0 || !ent.valid) return -1;
    if (ent.is_dir) return -1;

    char dir_part[EXT2_NAME_MAX], name_part[EXT2_NAME_MAX];
    char tmp[EXT2_NAME_MAX];
    int i;
    for (i = 0; path[i] && i < EXT2_NAME_MAX - 1; i++) tmp[i] = path[i];
    tmp[i] = 0;

    int last_slash = -1;
    for (i = 0; tmp[i]; i++) if (tmp[i] == '/') last_slash = i;

    if (last_slash < 0) { strcpy(dir_part, "/"); strcpy(name_part, tmp); }
    else {
        int j;
        for (j = 0; j < last_slash && j < EXT2_NAME_MAX - 1; j++) dir_part[j] = tmp[j];
        dir_part[j] = 0;
        if (dir_part[0] == 0) { dir_part[0] = '/'; dir_part[1] = 0; }
        int ni = 0;
        for (i = last_slash + 1; tmp[i] && ni < EXT2_NAME_MAX - 1; i++) name_part[ni++] = tmp[i];
        name_part[ni] = 0;
    }

    ext2_dirent_t parent_ent;
    if (ext2_find(dir_part, &parent_ent) < 0 || !parent_ent.valid)
        return -1;

    if (del_dirent(parent_ent.inode, name_part) < 0) return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(ent.inode, &inode) == 0) {
        inode.links_count--;
        if (inode.links_count <= 0) {
            free_blocks_by_inode(&inode);
            memset(&inode, 0, sizeof(inode));
        }
        ext2_write_inode(ent.inode, &inode);
        if (inode.links_count <= 0)
            free_inode(ent.inode);
    }
    return 0;
}

int ext2_rmdir(const char *path) {
    ext2_dirent_t ent;
    if (ext2_find(path, &ent) < 0 || !ent.valid) return -1;
    if (!ent.is_dir) return -1;

    /* Check if directory is empty (only . and ..) */
    struct ext2_inode inode;
    if (ext2_read_inode(ent.inode, &inode) < 0) return -1;

    int dir_size = inode.size;
    uint8_t *dir_buf = (uint8_t *)malloc(dir_size ? dir_size : 1);
    if (!dir_buf) return -1;
    if (dir_size > 0 && ext2_read_file(ent.inode, dir_buf, dir_size, 0) >= 0) {
        int off = 0;
        int real_entries = 0;
        while (off < dir_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(dir_buf + off);
            if (de->inode == 0) { off += de->rec_len; continue; }
            if (de->rec_len == 0) break;
            char de_name[EXT2_NAME_MAX];
            int nl = de->name_len < EXT2_NAME_MAX - 1 ? de->name_len : EXT2_NAME_MAX - 1;
            memcpy(de_name, de->name, nl);
            de_name[nl] = 0;
            if (strcmp(de_name, ".") != 0 && strcmp(de_name, "..") != 0)
                real_entries++;
            off += de->rec_len;
        }
        if (real_entries > 0) { free(dir_buf); return -1; }
    }
    free(dir_buf);

    char dir_part[EXT2_NAME_MAX], name_part[EXT2_NAME_MAX];
    char tmp[EXT2_NAME_MAX];
    int i;
    for (i = 0; path[i] && i < EXT2_NAME_MAX - 1; i++) tmp[i] = path[i];
    tmp[i] = 0;
    int last_slash = -1;
    for (i = 0; tmp[i]; i++) if (tmp[i] == '/') last_slash = i;

    if (last_slash < 0) { strcpy(dir_part, "/"); strcpy(name_part, tmp); }
    else {
        int j;
        for (j = 0; j < last_slash && j < EXT2_NAME_MAX - 1; j++) dir_part[j] = tmp[j];
        dir_part[j] = 0;
        if (dir_part[0] == 0) { dir_part[0] = '/'; dir_part[1] = 0; }
        int ni = 0;
        for (i = last_slash + 1; tmp[i] && ni < EXT2_NAME_MAX - 1; i++) name_part[ni++] = tmp[i];
        name_part[ni] = 0;
    }

    ext2_dirent_t parent_ent;
    if (ext2_find(dir_part, &parent_ent) < 0 || !parent_ent.valid) return -1;

    if (del_dirent(parent_ent.inode, name_part) < 0) return -1;

    free_blocks_by_inode(&inode);
    memset(&inode, 0, sizeof(inode));
    ext2_write_inode(ent.inode, &inode);
    free_inode(ent.inode);
    return 0;
}
