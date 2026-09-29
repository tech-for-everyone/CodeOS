#include "ext2.h"
#include "jbd2.h"
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
static uint8_t inode_buf[JBD2_MAX_BLOCK_SIZE];  /* Separate buffer for read_inode_block */

/* Zero the whole block buffer, then it is safe to write_block() it.
 *
 * Every freshly allocated block must be zeroed before use.  For a data
 * block the payload overwrites it anyway, but for an *indirect* block the
 * unwritten slots are the file's block pointers: leave them stale and
 * read_inode_block() hands back whatever the previous tenant of that block
 * left there, and the driver writes file data to a block number nobody
 * allocated.  That is not a leak, it is corruption -- a stale pointer of
 * 256 lands the write on the inode table and destroys it.
 *
 * This has to be block_buf_size, never sizeof(block_buf): block_buf is a
 * uint8_t *, so sizeof(block_buf) is a pointer width (8) and the obvious
 * spelling silently zeroes 8 bytes of a 1024-byte block.
 */
static void zero_block_buf(void) {
    if (!block_buf) return;
    memset(block_buf, 0, block_buf_size);
}

static int read_block(uint32_t block_num) {
    /* A journalled write is not on the disk yet -- it lives in the open
     * transaction's staging area until that transaction commits. So a
     * metadata block that this transaction has already modified has to be
     * served from there, or the reader gets a stale copy and makes a
     * plausible-looking but wrong decision with it: the block allocator
     * re-reading the block bitmap hands the same block to every allocation
     * in the transaction, which is how a 20-block file ends up with one
     * block stored thirteen times in its inode. */
    if (jbd2_peek_block(block_num, block_buf) == 0) return 0;

    uint32_t sector = (uint64_t)block_num * block_size / BLOCK_SECTOR_SIZE;
    uint32_t count = block_size / BLOCK_SECTOR_SIZE;
    if (count > 255) count = 255;
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    return block_read_sectors(p.start_lba + sector, (uint8_t)count, block_buf);
}

/* Read a block into a caller-supplied buffer, consulting the journal first.
 *
 * This is read_inode_block()'s path for indirect blocks, and it needs the same
 * staging-area lookup read_block() has.  An indirect block written earlier in
 * this transaction is not on disk yet, so without the peek a file that grows
 * past 12 blocks inside one transaction reads its own pointer block stale,
 * gets 0 for every slot past the written ones, and reallocates them. */
static int read_block_to_buf(uint32_t block_num, uint8_t *buf) {
    if (jbd2_peek_block(block_num, buf) == 0) return 0;
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    uint32_t sector = (uint64_t)block_num * block_size / BLOCK_SECTOR_SIZE;
    uint32_t count = block_size / BLOCK_SECTOR_SIZE;
    return block_read_sectors(p.start_lba + sector, (uint8_t)count, buf);
}

static int write_superblock(void);

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

    /* Initialize the journal if this is an ext3 image (journal inode present). */
    if (sb.journal_inum != 0) {
        /* Claim the filesystem as needing recovery before touching anything.
         *
         * EXT3_FEATURE_INCOMPAT_RECOVER (0x0004 in s_feature_incompat, not in
         * s_feature_compat where HAS_JOURNAL lives) is the "not cleanly
         * unmounted" flag, and it is set the moment a journal is written to
         * and cleared only by a real unmount that empties the journal.
         *
         * CodeOS has no unmount -- the VM is killed whenever the user stops it,
         * and there is no path that could checkpoint and clear the flag.  So
         * the flag is set here and never cleared, which is the honest state:
         * the journal may or may not be consistent, and the next mount has to
         * find out by replaying.
         *
         * Leaving it clear is not merely untidy.  e2fsck reads it as "clean
         * shutdown" and then *discards* the journal instead of replaying it
         * ("Superblock needs_recovery flag is clear, but journal has data"),
         * so any transaction that was committed but not yet checkpointed is
         * thrown away -- and its blocks are half-old, half-new, which is
         * precisely the corruption the journal existed to prevent. */
        sb.feature_incompat |= 0x0004;
        write_superblock();
        jbd2_init(sb.journal_inum);
    }

    ext2_mounted_val = 1;
    return 1;
}

int ext2_unmount(void) {
    if (!ext2_mounted_val) return 0;
    jbd2_shutdown(1);
    ext2_mounted_val = 0;
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

static int write_block(uint32_t block_num);

int ext2_read_inode(int inode_num, volatile struct ext2_inode *buf) {
    int bg = (inode_num - 1) / inodes_per_group;
    int idx = (inode_num - 1) % inodes_per_group;
    struct ext2_bg_desc bgd;
    if (read_bg_desc(bg, &bgd) < 0) return -1;
    uint32_t tbl_block = bgd.inode_table;
    uint32_t byte_off = idx * inode_size;
    if (block_size == 0) return -1;
    uint32_t block_num = tbl_block + byte_off / block_size;
    if (read_block(block_num) < 0) return -1;
    memcpy((void *)buf, block_buf + (byte_off % block_size), inode_size > 128 ? 128 : inode_size);
    return 0;
}

static uint32_t read_inode_block(struct ext2_inode *inode, int block_idx) {
    if (block_idx < 0) return 0;
    uint32_t uidx = (uint32_t)block_idx;
    if (uidx < 12) return inode->block[uidx];

    uint32_t ptrs_per_block = block_size / 4;
    if (uidx < 12 + ptrs_per_block) {
        if (inode->block[12] == 0) return 0;
        if (read_block_to_buf(inode->block[12], inode_buf) < 0) return 0;
        return ((uint32_t*)inode_buf)[uidx - 12];
    }

    uint32_t indirects = ptrs_per_block * ptrs_per_block;
    if (uidx < 12 + ptrs_per_block + indirects) {
        if (inode->block[13] == 0) return 0;
        if (read_block_to_buf(inode->block[13], inode_buf) < 0) return 0;
        uint32_t *indir = (uint32_t*)inode_buf;
        uint32_t i_idx = uidx - 12 - ptrs_per_block;
        if (indir[i_idx / ptrs_per_block] == 0) return 0;
        if (read_block_to_buf(indir[i_idx / ptrs_per_block], inode_buf) < 0) return 0;
        return ((uint32_t*)inode_buf)[i_idx % ptrs_per_block];
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



/* The single choke point for every filesystem block write.
 *
 * With a journal present the block is staged and NOT written here: the journal
 * writes the descriptor, the data, the commit block, and only then the home
 * location (see jbd2_flush()).  Until the transaction commits, the block's home
 * copy is stale, which is why read_block()/read_block_to_buf() must consult
 * jbd2_peek_block() first.
 *
 * jbd2_stage_block() returns non-zero when the caller must write the block
 * itself, which is the no-journal (journalless ext2) case. */
static int write_block(uint32_t block_num) {
    if (jbd2_stage_block(block_num, block_buf) == 0) return 0;
    uint32_t sector = (uint64_t)block_num * block_size / BLOCK_SECTOR_SIZE;
    uint32_t count = block_size / BLOCK_SECTOR_SIZE;
    return write_sectors(sector, count, block_buf);
}

/* Transaction boundaries for the operations that must be atomic. These are
 * no-ops returning 0 on a journalless ext2, where jbd2_txn_commit() would
 * otherwise report failure for work that in fact succeeded.
 *
 * The nesting is the point: jbd2_txn_begin() only lets the outermost pair own
 * the journal write, so ext2_write_file_path() can bracket truncate+write and
 * ext2_write_file() can bracket its own data writes without either knowing
 * whether it is the top-level call. */
static int ext2_txn_begin(void) {
    return jbd2_have_journal() ? jbd2_txn_begin() : 0;
}

static int ext2_txn_commit(void) {
    return jbd2_have_journal() ? jbd2_txn_commit() : 0;
}

/* ── transaction boundaries ───────────────────────────────────────────────
 *
 * Every operation that changes more than one block has to reach the log as a
 * unit, or a crash in the middle leaves the filesystem describing itself
 * inconsistently: a block marked allocated in the bitmap with no inode
 * pointing at it, a directory entry naming an inode that was never written, a
 * freed block whose data is still referenced.  None of those are detectable as
 * a write in progress -- each is a plausible-looking filesystem that e2fsck
 * will eventually call corrupt.
 *
 * The bodies live in *_impl() functions so that the commit cannot be skipped by
 * an early return.  Commit runs on the error paths too, and that is deliberate:
 * an operation that fails partway has usually already staged the frees that
 * undo its own work, and those belong in the log as well.  Leaving them staged
 * for the next transaction to flush would be equally consistent on disk, but
 * it would group unrelated operations into one atomic unit, which is not what
 * a caller of unlink() asked for.
 *
 * Nesting is handled by jbd2 itself: only the outermost begin/commit pair owns
 * the journal write, so ext2_write_file_path() bracketing truncate+write and
 * ext2_write_file() bracketing its own data writes compose into one
 * transaction without either knowing which one it is.
 */
static int ext2_write_file_impl(int inode_num, const void *buf, int max, int offset);
static int ext2_write_file_path_impl(const char *path, const void *buf, int max);
static int ext2_mkdir_impl(const char *path);
static int ext2_creat_impl(const char *path);
static int ext2_unlink_impl(const char *path);
static int ext2_rmdir_impl(const char *path);

int ext2_write_file(int inode_num, const void *buf, int max, int offset) {
    if (ext2_txn_begin() < 0) return ext2_write_file_impl(inode_num, buf, max, offset);
    int r = ext2_write_file_impl(inode_num, buf, max, offset);
    ext2_txn_commit();
    return r;
}

int ext2_write_file_path(const char *path, const void *buf, int max) {
    if (ext2_txn_begin() < 0) return ext2_write_file_path_impl(path, buf, max);
    int r = ext2_write_file_path_impl(path, buf, max);
    ext2_txn_commit();
    return r;
}

int ext2_mkdir(const char *path) {
    if (ext2_txn_begin() < 0) return ext2_mkdir_impl(path);
    int r = ext2_mkdir_impl(path);
    ext2_txn_commit();
    return r;
}

int ext2_creat(const char *path) {
    if (ext2_txn_begin() < 0) return ext2_creat_impl(path);
    int r = ext2_creat_impl(path);
    ext2_txn_commit();
    return r;
}

int ext2_unlink(const char *path) {
    if (ext2_txn_begin() < 0) return ext2_unlink_impl(path);
    int r = ext2_unlink_impl(path);
    ext2_txn_commit();
    return r;
}

int ext2_rmdir(const char *path) {
    if (ext2_txn_begin() < 0) return ext2_rmdir_impl(path);
    int r = ext2_rmdir_impl(path);
    ext2_txn_commit();
    return r;
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

/* Keep a group's directory count in step with its inode table.
 *
 * e2fsck recounts the directories in each group from the inode table and
 * compares: "Directories count wrong for group #0 (4, counted=5)".  The
 * allocator and the inode allocator both maintain their group's free counts,
 * so the directory count being left alone is an omission rather than a
 * deliberate simplification -- and it matters beyond tidiness, because a
 * real `e2fsck -y` resolves the disagreement by rewriting the group
 * descriptor, so the driver and e2fsck then disagree about what is on disk.
 *
 * The count belongs to the group holding the directory's *inode*, not the
 * group holding its data blocks, because that is what e2fsck counts.  The two
 * can differ: a directory's blocks are allocated with the inode's own group as
 * the goal, but a 2 KiB directory spans into the next group. */
static void bump_dir_count(int inode_num, int delta) {
    if (inode_num <= 0) return;
    uint32_t bg = (inode_num - 1) / inodes_per_group;
    struct ext2_bg_desc bgd;
    if (read_bg_desc(bg, &bgd) < 0) return;
    if (delta > 0) {
        if (bgd.used_dirs_count != 0xFFFF) bgd.used_dirs_count++;
    } else if (bgd.used_dirs_count > 0) {
        bgd.used_dirs_count--;
    }
    write_bg_desc(bg, &bgd);
}

int ext2_write_inode(int inode_num, const volatile struct ext2_inode *buf) {
    int bg = (inode_num - 1) / inodes_per_group;
    int idx = (inode_num - 1) % inodes_per_group;
    struct ext2_bg_desc bgd;
    if (read_bg_desc(bg, &bgd) < 0) return -1;
    uint32_t tbl_block = bgd.inode_table;
    uint32_t byte_off = idx * inode_size;
    uint32_t block_num = tbl_block + byte_off / block_size;
    if (read_block(block_num) < 0) return -1;
    memcpy(block_buf + (byte_off % block_size), (const void *)buf, inode_size > 128 ? 128 : inode_size);
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

static uint32_t bg_first_block(uint32_t bg) {
    return bg * blocks_per_group + sb.first_data_block;
}

static uint32_t alloc_block_goal(int preferred_bg) {
    uint32_t bg_count = (sb.blocks_count + blocks_per_group - 1) / blocks_per_group;
    if (preferred_bg < 0 || (uint32_t)preferred_bg >= bg_count) preferred_bg = 0;

    for (uint32_t scan = 0; scan < bg_count; scan++) {
        uint32_t bg = (preferred_bg + scan) % bg_count;
        struct ext2_bg_desc bgd;
        if (read_bg_desc(bg, &bgd) < 0) continue;
        if (bgd.free_blocks_count == 0) continue;

        uint32_t first = bg_first_block(bg);
        uint32_t blocks_in_bg = blocks_per_group;
        if (bg == bg_count - 1) {
            if (first + blocks_in_bg > sb.blocks_count)
                blocks_in_bg = sb.blocks_count - first;
        }

        uint32_t bitmap_block = bgd.block_bitmap;
        for (uint32_t byte_idx = 0; byte_idx < block_size && byte_idx * 8 < blocks_in_bg; byte_idx++) {
            uint32_t read_block_num = bitmap_block + byte_idx / block_size;
            if (read_block(read_block_num) < 0) break;

            if (block_buf[byte_idx % block_size] == 0xFF) continue;

            for (int bit = 0; bit < 8 && (int)(byte_idx * 8 + bit) < (int)blocks_in_bg; bit++) {
                if (!(block_buf[byte_idx % block_size] & (1 << bit))) {
                    uint32_t abs_block = first + byte_idx * 8 + bit;
                    if (abs_block >= sb.blocks_count) continue;

                    set_block_bit(bg, byte_idx * 8 + bit, 1);

                    bgd.free_blocks_count--;
                    write_bg_desc(bg, &bgd);

                    sb.free_blocks_count--;
                    write_superblock();

                    zero_block_buf();
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
    uint32_t bg = (phys_block - sb.first_data_block) / blocks_per_group;
    struct ext2_bg_desc bgd;
    read_bg_desc(bg, &bgd);
    uint32_t bit = phys_block - bg_first_block(bg);
    set_block_bit(bg, bit, 0);
    bgd.free_blocks_count++;
    write_bg_desc(bg, &bgd);
    sb.free_blocks_count++;
    write_superblock();
}

/* Every block to free, gathered before any of them is freed.
 *
 * The gathering is not an optimisation, it is the fix.  free_block() works
 * through block_buf -- it reads the group descriptor and the block bitmap into
 * it in order to clear the bit -- so a loop that iterates an indirect block's
 * entries out of block_buf has its buffer overwritten by the first
 * free_block() call.  Every entry after the first is then read out of a block
 * bitmap, handed to free_block() as a garbage block number, and the real block
 * is leaked: still marked used, with no inode pointing at it.  That is exactly
 * what e2fsck reports as "Block bitmap differences: -NNNN", and it is how the
 * 8 single-indirect data blocks of the 20-block fstest file leaked on every
 * re-run of the test.  The garbage frees are the worse direction: they mark
 * live blocks free, so the next allocation hands out a block that is still in
 * use. */
typedef struct {
    uint32_t *v;
    uint32_t  n;
    uint32_t  cap;
} free_list_t;

static void freelist_add(free_list_t *l, uint32_t b) {
    if (b == 0) return;
    if (l->n == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2 : 64;
        uint32_t *v = (uint32_t *)realloc(l->v, (size_t)cap * sizeof(uint32_t));
        if (!v) return;              /* leak rather than free the wrong block */
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = b;
}

static void freelist_release(free_list_t *l) {
    for (uint32_t i = 0; i < l->n; i++) free_block(l->v[i]);
    free(l->v);
    l->v = 0;
    l->n = 0;
    l->cap = 0;
}

/* Append the data blocks named by the indirect block at `blk`.
 *
 * depth 1: entries are data blocks, so they go straight on the list.
 * depth 2: entries are single-indirect blocks, so they go on the list too and
 *          are then descended into.  The descent reads into inode_buf again, so
 * it must be driven from the list (heap) and never from the buffer being
 * iterated -- hence the second loop, and re-reading l->v[i] each time because
 * the recursion can realloc. */
static void collect_indirect(free_list_t *l, uint32_t blk, int depth) {
    if (blk == 0) return;
    if (read_block_to_buf(blk, inode_buf) < 0) return;

    uint32_t ptrs = block_size / 4;
    uint32_t *p = (uint32_t *)inode_buf;
    uint32_t first = l->n;
    for (uint32_t i = 0; i < ptrs; i++)
        freelist_add(l, p[i]);
    if (depth != 2) return;
    for (uint32_t i = first; i < l->n; i++)
        collect_indirect(l, l->v[i], 1);
}

static void free_blocks_by_inode(struct ext2_inode *inode) {
    free_list_t l = { 0, 0, 0 };

    /* The direct pointers live in the inode, not in a shared buffer, so these
     * are safe to free one at a time. */
    for (int i = 0; i < 12; i++) {
        if (inode->block[i] == 0) continue;
        free_block(inode->block[i]);
        inode->block[i] = 0;
    }

    if (inode->block[12]) {
        collect_indirect(&l, inode->block[12], 1);
        freelist_add(&l, inode->block[12]);
        inode->block[12] = 0;
    }
    if (inode->block[13]) {
        collect_indirect(&l, inode->block[13], 2);
        freelist_add(&l, inode->block[13]);
        inode->block[13] = 0;
    }

    freelist_release(&l);
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

/* Fetch (allocating if needed) the sub-indirect block at parent_index.
 * `inode` is charged for the block when one is allocated, because i_blocks
 * counts every block the inode owns -- the pointer blocks as well as the
 * data.  e2fsck recomputes i_blocks and reports the difference. */
static uint32_t alloc_or_get_indir_block(uint32_t parent_block, uint32_t parent_index,
                                         int inode_num, struct ext2_inode *inode) {
    uint32_t blk;
    if (parent_block == 0) {
        blk = alloc_block_for_inode(inode_num);
        if (blk == 0) return 0;
        zero_block_buf();
        write_block(blk);
        inode->blocks += block_size / 512;
        return blk;
    }
    if (read_block(parent_block) < 0) return 0;
    blk = ((uint32_t *)block_buf)[parent_index];
    if (blk != 0) return blk;
    blk = alloc_block_for_inode(inode_num);
    if (blk == 0) return 0;
    zero_block_buf();
    write_block(blk);
    write_indir_ptr(parent_block, parent_index, blk);
    inode->blocks += block_size / 512;
    return blk;
}

static int ext2_write_file_impl(int inode_num, const void *buf, int max, int offset) {
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
                        zero_block_buf();
                        write_block(inode.block[12]);
                        inode.blocks += block_size / 512;
                    }
                    write_indir_ptr(inode.block[12], ind_idx, phys_block);
                } else {
                    uint32_t dind_idx = (ind_idx - ptrs_per_block) / ptrs_per_block;
                    uint32_t ind_idx2 = (ind_idx - ptrs_per_block) % ptrs_per_block;
                    if (inode.block[13] == 0) {
                        inode.block[13] = alloc_block_for_inode(inode_num);
                        if (inode.block[13] == 0) return -1;
                        zero_block_buf();
                        write_block(inode.block[13]);
                        inode.blocks += block_size / 512;
                    }
                    uint32_t ind_block = alloc_or_get_indir_block(inode.block[13], dind_idx, inode_num, &inode);
                    if (ind_block == 0) return -1;
                    write_indir_ptr(ind_block, ind_idx2, phys_block);
                }
            }

            inode.blocks += block_size / 512;
        }

        zero_block_buf();
        memcpy(block_buf + block_off, (const uint8_t *)buf + total, copy);
        write_block(phys_block);

        total += copy;
        pos += copy;
    }

    if (new_size > (int)inode.size) inode.size = new_size;
    ext2_write_inode(inode_num, &inode);
    return total;
}

static int ext2_write_file_path_impl(const char *path, const void *buf, int max) {
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
        off = 0;
        while (off < dir_size) {
            struct ext2_dirent *de = (struct ext2_dirent *)(dir_buf + off);
            if (de->inode == 0) { off += de->rec_len; continue; }
            if (de->rec_len == 0) break;
            last_off = off;
            off += de->rec_len;
        }
        struct ext2_dirent *last = (struct ext2_dirent *)(dir_buf + last_off);
        int old_name_len = last->name_len;
        int old_entry_size = (sizeof(struct ext2_dirent) + old_name_len + 3) & ~3;
        if (old_entry_size < 8) old_entry_size = 8;
        int old_rec = last->rec_len;

        /* The space this split actually leaves is old_rec - old_entry_size.
         * Deriving it from the same old_entry_size the split below installs is
         * the whole point: measuring against the unrounded 8 + name_len
         * overstates the room by up to 3 bytes plus the rounding, so the test
         * could pass with a name that does not fit.  The new entry would then
         * be written with rec_len < 8, or would overrun into the entry after
         * it, and the directory's rec_len chain breaks -- which is what makes
         * e2fsck call the directory corrupt and makes every later lookup in it
         * miss. */
        int free_space = old_rec - old_entry_size;
        if (free_space >= entry_size) {
            /* Shorten last entry to its actual size; new entry takes the rest. */
            last->rec_len = old_entry_size;
            off = last_off + old_entry_size;
            struct ext2_dirent *new_de = (struct ext2_dirent *)(dir_buf + off);
            new_de->inode = new_inode;
            new_de->rec_len = old_rec - old_entry_size;
            new_de->name_len = name_len;
            new_de->file_type = file_type;
            memcpy(new_de->name, name, name_len);
            int r = ext2_write_file(dir_inode, dir_buf, dir_size, 0);
            free(dir_buf);
            return r;
        }
    }

    /* No room left in the existing blocks: grow the directory by one whole
     * block.
     *
     * This used to grow by entry_size, which is wrong twice over.
     *
     * A directory's i_size is always a multiple of the block size, so an inode
     * of 1228 bytes is malformed on its face -- e2fsck rejects the whole
     * filesystem with "i_size is 1228, should be 2048".
     *
     * And the new entry was written at the new end of the buffer with
     * rec_len == entry_size and nothing after it, so the new block had no
     * terminating entry.  A walker steps off the end of the real entries onto
     * rec_len == 0 and stops there, which e2fsck reports as "directory
     * corrupted" and which silently hides every entry that would have come
     * after it.  Filling a directory one entry at a time is enough to hit
     * this: 100 short names need 1200 bytes of entry space against a 1 KiB
     * block.
     *
     * So the new block holds the new entry followed by a free entry whose
     * rec_len runs to the end of the block, which is what terminates the chain.
     * The previous block needs no adjustment: we only reach here when its last
     * entry already spans to the block end, because a last entry with a
     * usable free tail would have been split above. */
    if (dir_size % block_size != 0) {
        /* Unreachable for any directory this driver wrote, because every path
         * above grows by whole blocks.  If it ever happens the directory is
         * already malformed, and appending into it would compound the damage
         * rather than repair it -- the entry would land mid-block, and the
         * partial block it lands in has no terminating entry either. */
        free(dir_buf);
        return -1;
    }
    int new_size = dir_size + block_size;
    uint8_t *tmp = (uint8_t *)malloc((size_t)new_size);
    if (!tmp) { free(dir_buf); return -1; }
    memset(tmp, 0, (size_t)new_size);
    if (dir_size > 0) memcpy(tmp, dir_buf, (size_t)dir_size);

    struct ext2_dirent *new_de = (struct ext2_dirent *)(tmp + dir_size);
    new_de->inode = new_inode;
    new_de->rec_len = entry_size;
    new_de->name_len = name_len;
    new_de->file_type = file_type;
    memcpy(new_de->name, name, name_len);

    struct ext2_dirent *tail = (struct ext2_dirent *)(tmp + dir_size + entry_size);
    tail->inode = 0;
    tail->rec_len = (uint16_t)(block_size - entry_size);
    tail->name_len = 0;
    tail->file_type = 0;

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

static int ext2_mkdir_impl(const char *path) {
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
    zero_block_buf();

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

    /* Only now is it a directory rather than a free inode, so this is the
     * point at which the group's directory count has to follow. */
    bump_dir_count(inode_num, +1);

    /* A directory's links_count is 2 (for "." and "..") plus one per
     * subdirectory it holds, so creating a subdirectory has to bump the
     * parent's.  e2fsck recomputes the count from the directory's contents and
     * reports the difference; leaving it alone makes every mkdir leave a
     * filesystem that a real e2fsck wants to "fix". */
    struct ext2_inode parent;
    if (ext2_read_inode(parent_ent.inode, &parent) == 0) {
        parent.links_count++;
        ext2_write_inode(parent_ent.inode, &parent);
    }

    return 0;
}

static int ext2_creat_impl(const char *path) {
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

static int ext2_unlink_impl(const char *path) {
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

static int ext2_rmdir_impl(const char *path) {
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

    /* Mirror of the increment in ext2_mkdir: the parent loses a subdirectory,
     * so its links_count goes back down.  See the comment there. */
    struct ext2_inode parent;
    if (ext2_read_inode(parent_ent.inode, &parent) == 0 && parent.links_count > 0) {
        parent.links_count--;
        ext2_write_inode(parent_ent.inode, &parent);
    }

    free_blocks_by_inode(&inode);
    memset(&inode, 0, sizeof(inode));
    ext2_write_inode(ent.inode, &inode);
    free_inode(ent.inode);

    /* After the inode is released, so it cannot be double-counted by a
     * concurrent walk -- there is no concurrency, but the ordering keeps the
     * two operations adjacent in the journal, which matters once replay is
     * what recovers the filesystem. */
    bump_dir_count(ent.inode, -1);
    return 0;
}

/* ── raw block access for the journal ────────────────────────────
 * jbd2.c must move bytes between the journal and the filesystem without
 * disturbing block_buf, which callers still hold across the call. These
 * move bytes without touching the shared buffer.
 */
int ext2_read_block_from(uint32_t block_num, void *dst) {
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    return block_read_sectors(p.start_lba + block_num * block_size / BLOCK_SECTOR_SIZE,
                                (uint8_t)(block_size / BLOCK_SECTOR_SIZE), dst);
}

int ext2_write_block_from(uint32_t block_num, const void *src) {
    return write_sectors(block_num * block_size / BLOCK_SECTOR_SIZE,
                            block_size / BLOCK_SECTOR_SIZE, src);
}

uint32_t ext2_inode_phys_block(int inode_num, int block_idx) {
    struct ext2_inode inode;
    if (ext2_read_inode(inode_num, &inode) < 0) return 0;
    return read_inode_block(&inode, block_idx);
}

uint32_t ext2_block_count(void) { return sb.blocks_count; }
uint32_t ext2_block_size(void) { return block_size; }
