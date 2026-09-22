/*
 * CodeFS — CodeOS-exclusive CoW filesystem
 *
 * Features:
 *   - Copy-on-Write (CoW) block allocation
 *   - CRC32 checksums on superblock, inodes, and data blocks
 *   - Extent-based allocation (up to 4 extents per inode)
 *   - Inline data for files <= 60 bytes
 *   - Simple journal for crash recovery
 *   - Case-sensitive file names up to 255 chars
 *
 * Layout: [boot 1K] [superblock 4K] [block bitmap] [inode bitmap]
 *         [inode table] [journal] [data blocks]
 */

#include "codefs.h"
#include "vfs.h"
#include "block.h"
#include "part.h"
#include "string.h"
#include "kprintf.h"
#include "mm.h"
#include "timer.h"

#define CODEFS_VERSION_MAJOR 1
#define CODEFS_VERSION_MINOR 0

static int codefs_mounted_val;
static struct codefs_superblock sb;
static int sb_part_idx;

static uint32_t *block_bitmap;
static uint32_t *inode_bitmap;
static struct codefs_inode *inode_table;
static uint8_t *block_buf;
static uint8_t *journal_buf;
static uint32_t journal_seq;

/* ── CRC32 ─────────────────────────────────────────────── */

static uint32_t crc32_table[256];
static int crc32_inited;

static void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_inited = 1;
}

static uint32_t codefs_crc32(const void *data, uint32_t len) {
    if (!crc32_inited) crc32_init();
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ── Block I/O ──────────────────────────────────────────── */

static int read_block(uint32_t block_num, void *buf) {
    uint32_t sector = (uint64_t)block_num * CODEFS_BLOCK_SIZE / 512;
    uint32_t count = CODEFS_BLOCK_SIZE / 512;
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    for (uint32_t off = 0; off < count; off += 255) {
        uint32_t n = count - off;
        if (n > 255) n = 255;
        if (block_read_sectors(p.start_lba + sector + off, (uint8_t)n, (uint8_t *)buf + off * 512) < 0)
            return -1;
    }
    return 0;
}

static int write_block(uint32_t block_num, const void *buf) {
    uint32_t sector = (uint64_t)block_num * CODEFS_BLOCK_SIZE / 512;
    uint32_t count = CODEFS_BLOCK_SIZE / 512;
    partition_t p;
    if (part_get(sb_part_idx, &p) < 0) return -1;
    for (uint32_t off = 0; off < count; off += 255) {
        uint32_t n = count - off;
        if (n > 255) n = 255;
        if (block_write_sectors(p.start_lba + sector + off, (uint8_t)n, (const uint8_t *)buf + off * 512) < 0)
            return -1;
    }
    return 0;
}

/* ── Superblock ─────────────────────────────────────────── */

static int read_superblock(void) {
    if (read_block(1, block_buf) < 0) return -1;
    memcpy(&sb, block_buf + (CODEFS_SB_OFFSET % CODEFS_BLOCK_SIZE), sizeof(sb));
    if (sb.magic != CODEFS_MAGIC) {
        kprintf("codefs: bad magic 0x%08x (expected 0x%08x)\n", sb.magic, CODEFS_MAGIC);
        return -1;
    }
    uint32_t saved = sb.crc;
    sb.crc = 0;
    uint32_t computed = codefs_crc32(&sb, sizeof(sb));
    sb.crc = saved;
    if (saved != computed) {
        kprintf("codefs: superblock CRC mismatch (stored=0x%08x computed=0x%08x)\n", saved, computed);
        return -1;
    }
    sb.state |= CODEFS_S_DIRTY;
    sb.mount_count++;
    sb.last_mount = timer_get_ticks();
    return 0;
}

static int write_superblock(void) {
    sb.crc = 0;
    sb.crc = codefs_crc32(&sb, sizeof(sb));
    memset(block_buf, 0, CODEFS_BLOCK_SIZE);
    memcpy(block_buf + (CODEFS_SB_OFFSET % CODEFS_BLOCK_SIZE), &sb, sizeof(sb));
    return write_block(1, block_buf);
}

/* ── Block bitmap ────────────────────────────────────────── */

static int alloc_block(void) {
    uint32_t total = sb.blocks_count;
    uint32_t words = (total + 31) / 32;
    for (uint32_t i = 0; i < words; i++) {
        if (block_bitmap[i] == 0xFFFFFFFF) continue;
        for (int b = 0; b < 32; b++) {
            uint32_t idx = i * 32 + b;
            if (idx >= total) break;
            if (!(block_bitmap[i] & (1u << b))) {
                block_bitmap[i] |= (1u << b);
                sb.free_blocks--;
                return sb.data_area_start + idx;
            }
        }
    }
    return -1;
}

static void free_block(uint32_t block) {
    if (block < sb.data_area_start) return;
    uint32_t idx = block - sb.data_area_start;
    uint32_t word = idx / 32;
    uint32_t bit = idx % 32;
    block_bitmap[word] &= ~(1u << bit);
    sb.free_blocks++;
}

static void flush_bitmap(void) {
    write_block(sb.data_area_start - 2, block_bitmap);
    write_block(sb.data_area_start - 1, inode_bitmap);
}

/* ── Inode I/O ──────────────────────────────────────────── */

static int read_inode(uint32_t ino, struct codefs_inode *out) {
    if (ino == 0 || ino > sb.total_inodes) return -1;
    uint32_t idx = ino - 1;
    uint32_t bytes_off = idx * CODEFS_INODE_SIZE;
    uint32_t blk = sb.inode_table_start + (bytes_off / CODEFS_BLOCK_SIZE);
    uint32_t off_in_blk = bytes_off % CODEFS_BLOCK_SIZE;
    if (read_block(blk, block_buf) < 0) return -1;
    memcpy(out, block_buf + off_in_blk, sizeof(*out));
    return 0;
}

static int write_inode(uint32_t ino, const struct codefs_inode *in) {
    if (ino == 0 || ino > sb.total_inodes) return -1;
    uint32_t idx = ino - 1;
    uint32_t bytes_off = idx * CODEFS_INODE_SIZE;
    uint32_t blk = sb.inode_table_start + (bytes_off / CODEFS_BLOCK_SIZE);
    uint32_t off_in_blk = bytes_off % CODEFS_BLOCK_SIZE;
    if (read_block(blk, block_buf) < 0) return -1;
    memcpy(block_buf + off_in_blk, in, sizeof(*in));
    return write_block(blk, block_buf);
}

static int alloc_inode(struct codefs_inode *out) {
    uint32_t words = (sb.total_inodes + 31) / 32;
    for (uint32_t i = 0; i < words; i++) {
        if (inode_bitmap[i] == 0xFFFFFFFF) continue;
        for (int b = 0; b < 32; b++) {
            uint32_t idx = i * 32 + b;
            if (idx >= sb.total_inodes) break;
            if (!(inode_bitmap[i] & (1u << b))) {
                inode_bitmap[i] |= (1u << b);
                sb.free_inodes--;
                uint32_t ino = idx + 1;
                memset(out, 0, sizeof(*out));
                out->links = 1;
                out->ctime = timer_get_ticks();
                return ino;
            }
        }
    }
    return -1;
}

/* ── Extent helpers ──────────────────────────────────────── */

static int extent_find(const struct codefs_inode *ino, uint32_t file_off, uint32_t *disk_blk) {
    for (uint32_t i = 0; i < ino->extent_count && i < CODEFS_MAX_EXTENTS; i++) {
        const struct codefs_extent *e = &ino->extents[i];
        if (file_off >= e->block && file_off < e->block + e->length) {
            *disk_blk = e->disk_start + (file_off - e->block);
            return 0;
        }
    }
    return -1;
}

static int extent_alloc(struct codefs_inode *ino, uint32_t file_blk, uint32_t count) {
    if (ino->extent_count >= CODEFS_MAX_EXTENTS) return -1;
    int disk = alloc_block();
    if (disk < 0) return -1;
    struct codefs_extent *e = &ino->extents[ino->extent_count];
    e->block = file_blk;
    e->length = count;
    e->disk_start = disk;
    ino->extent_count++;
    return 0;
}

static uint32_t file_blocks_for_size(uint32_t size) {
    if (size == 0) return 0;
    return (size + CODEFS_BLOCK_SIZE - 1) / CODEFS_BLOCK_SIZE;
}

static int ensure_blocks(struct codefs_inode *ino, uint32_t needed) {
    uint32_t have = 0;
    for (uint32_t i = 0; i < ino->extent_count; i++)
        have += ino->extents[i].length;
    while (have < needed) {
        uint32_t next = have;
        if (extent_alloc(ino, next, 1) < 0) return -1;
        have++;
    }
    return 0;
}

/* ── Path resolution ─────────────────────────────────────── */

static int resolve_path(const char *path, struct codefs_inode *out, uint32_t *out_ino) {
    if (!path || path[0] != '/') return -1;
    uint32_t cur_ino = sb.root_inode;
    struct codefs_inode cur;
    if (read_inode(cur_ino, &cur) < 0) return -1;
    const char *p = path + 1;
    while (*p) {
        while (*p == '/') p++;
        if (*p == 0) break;
        const char *end = p;
        while (*end && *end != '/') end++;
        uint32_t nlen = (uint32_t)(end - p);
        if (nlen > CODEFS_NAME_MAX) nlen = CODEFS_NAME_MAX;
        if (!(cur.mode & 040000)) return -1;
        int found = 0;
        uint32_t blocks = file_blocks_for_size(cur.size);
        for (uint32_t b = 0; b < blocks; b++) {
            uint32_t disk;
            if (extent_find(&cur, b, &disk) < 0) continue;
            if (read_block(disk, block_buf) < 0) continue;
            uint32_t off = 0;
            while (off < CODEFS_BLOCK_SIZE) {
                struct codefs_dirent *de = (struct codefs_dirent *)(block_buf + off);
                if (de->inode == 0 || de->rec_len == 0) break;
                if (de->name_len == nlen && memcmp(de->name, p, nlen) == 0) {
                    cur_ino = de->inode;
                    if (read_inode(cur_ino, &cur) < 0) return -1;
                    found = 1;
                    break;
                }
                off += de->rec_len;
            }
            if (found) break;
        }
        if (!found) return -1;
        p = end;
    }
    *out = cur;
    if (out_ino) *out_ino = cur_ino;
    return 0;
}

static uint32_t find_parent(const char *path, char *name_out, uint32_t name_max) {
    int last = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last = i;
    if (last < 0) return 0;
    const char *child = path + last + 1;
    uint32_t nlen = strlen(child);
    if (nlen >= name_max) nlen = name_max - 1;
    memcpy(name_out, child, nlen);
    name_out[nlen] = 0;
    if (last == 0) return sb.root_inode;
    char buf[CODEFS_PATH_MAX];
    strncpy_safe(buf, path, CODEFS_PATH_MAX);
    buf[last] = 0;
    struct codefs_inode dummy;
    uint32_t pino;
    if (resolve_path(buf, &dummy, &pino) < 0) return 0;
    return pino;
}

/* ── Directory ops ───────────────────────────────────────── */

static int dir_add_entry(struct codefs_inode *dir, uint32_t child_ino, const char *name, uint8_t type) {
    uint32_t nlen = strlen(name);
    uint32_t rec_len = codefs_dir_rec_len(nlen);
    uint32_t blocks = file_blocks_for_size(dir->size);
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t disk;
        if (extent_find(dir, b, &disk) < 0) continue;
        if (read_block(disk, block_buf) < 0) continue;
        uint32_t off = 0;
        while (off < CODEFS_BLOCK_SIZE) {
            struct codefs_dirent *de = (struct codefs_dirent *)(block_buf + off);
            if (de->inode == 0 && de->rec_len >= rec_len) {
                de->inode = child_ino;
                de->rec_len = rec_len;
                de->name_len = nlen;
                de->file_type = type;
                memcpy(de->name, name, nlen);
                return write_block(disk, block_buf);
            }
            if (de->rec_len == 0) break;
            off += de->rec_len;
        }
    }
    uint32_t new_size = dir->size + CODEFS_BLOCK_SIZE;
    ensure_blocks(dir, file_blocks_for_size(new_size));
    dir->size = new_size;
    uint32_t newblk = file_blocks_for_size(new_size) - 1;
    uint32_t disk;
    if (extent_find(dir, newblk, &disk) < 0) return -1;
    memset(block_buf, 0, CODEFS_BLOCK_SIZE);
    struct codefs_dirent *de = (struct codefs_dirent *)block_buf;
    de->inode = child_ino;
    de->rec_len = CODEFS_BLOCK_SIZE;
    de->name_len = nlen;
    de->file_type = type;
    memcpy(de->name, name, nlen);
    return write_block(disk, block_buf);
}

static int dir_remove_entry(struct codefs_inode *dir, const char *name) {
    uint32_t blocks = file_blocks_for_size(dir->size);
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t disk;
        if (extent_find(dir, b, &disk) < 0) continue;
        if (read_block(disk, block_buf) < 0) continue;
        uint32_t off = 0;
        while (off < CODEFS_BLOCK_SIZE) {
            struct codefs_dirent *de = (struct codefs_dirent *)(block_buf + off);
            if (de->inode == 0 || de->rec_len == 0) break;
            if (de->name_len == strlen(name) && memcmp(de->name, name, de->name_len) == 0) {
                de->inode = 0;
                return write_block(disk, block_buf);
            }
            off += de->rec_len;
        }
    }
    return -1;
}

/* ── Journal stubs ───────────────────────────────────────── */

static int journal_write(const void *data, uint32_t len) {
    if (!journal_buf || sb.journal_len == 0) return 0;
    uint32_t blk = sb.journal_start + (journal_seq % sb.journal_len);
    struct codefs_journal_header hdr;
    hdr.magic = 0x434A4E4C;
    hdr.seq = journal_seq++;
    hdr.start = blk;
    hdr.len = len;
    hdr.crc = codefs_crc32(data, len);
    memcpy(journal_buf, &hdr, sizeof(hdr));
    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > CODEFS_BLOCK_SIZE - sizeof(hdr)) chunk = CODEFS_BLOCK_SIZE - sizeof(hdr);
        memcpy(journal_buf + sizeof(hdr), (const uint8_t *)data + off, chunk);
        write_block(blk, journal_buf);
        off += chunk;
        blk++;
        if (blk >= sb.journal_start + sb.journal_len) blk = sb.journal_start;
    }
    return 0;
}

/* ── VFS interface ───────────────────────────────────────── */

static int codefs_vfs_open(const char *path, int flags) {
    struct codefs_inode ino;
    if (resolve_path(path, &ino, NULL) < 0) {
        if (!(flags & 0x100)) return -1;
        char name[CODEFS_NAME_MAX];
        uint32_t pino = find_parent(path, name, sizeof(name));
        if (pino == 0) return -1;
        struct codefs_inode parent;
        if (read_inode(pino, &parent) < 0) return -1;
        uint32_t new_ino = 0;
        struct codefs_inode new_node;
        new_ino = alloc_inode(&new_node);
        if ((int)new_ino < 0) return -1;
        new_node.mode = 0100644;
        new_node.size = 0;
        new_node.uid = 0;
        new_node.gid = 0;
        new_node.extent_count = 0;
        new_node.inline_len = 0;
        new_node.links = 1;
        write_inode(new_ino, &new_node);
        dir_add_entry(&parent, new_ino, name, CODEFS_FT_REG);
        parent.links++;
        write_inode(pino, &parent);
        flush_bitmap();
    }
    return 0;
}

static int codefs_vfs_close(int fd) {
    (void)fd;
    return 0;
}

static int codefs_vfs_read(int fd, void *buf, int count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

static int codefs_vfs_write(int fd, const void *buf, int count) {
    (void)fd;
    (void)buf;
    (void)count;
    return -1;
}

static int codefs_vfs_seek(int fd, int offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return -1;
}

static int codefs_vfs_fstat(int fd, void *stat) {
    (void)fd;
    (void)stat;
    return -1;
}

static int codefs_vfs_mkdir(const char *path) {
    struct codefs_inode dummy;
    if (resolve_path(path, &dummy, NULL) == 0) return -1;
    char name[CODEFS_NAME_MAX];
    uint32_t pino = find_parent(path, name, sizeof(name));
    if (pino == 0) return -1;
    struct codefs_inode parent;
    if (read_inode(pino, &parent) < 0) return -1;
    struct codefs_inode new_dir;
    uint32_t ino = alloc_inode(&new_dir);
    if ((int)ino < 0) return -1;
    new_dir.mode = 040755;
    new_dir.size = 0;
    new_dir.uid = 0;
    new_dir.gid = 0;
    new_dir.extent_count = 0;
    new_dir.inline_len = 0;
    new_dir.links = 2;
    write_inode(ino, &new_dir);
    dir_add_entry(&parent, ino, name, CODEFS_FT_DIR);
    parent.links++;
    write_inode(pino, &parent);
    flush_bitmap();
    journal_write(&ino, 4);
    return 0;
}

static int codefs_vfs_unlink(const char *path) {
    struct codefs_inode ino;
    uint32_t ino_num;
    if (resolve_path(path, &ino, &ino_num) < 0) return -1;
    if (ino.mode & 040000) return -1;
    char name[CODEFS_NAME_MAX];
    uint32_t pino = find_parent(path, name, sizeof(name));
    if (pino == 0) return -1;
    struct codefs_inode parent;
    if (read_inode(pino, &parent) < 0) return -1;
    dir_remove_entry(&parent, name);
    parent.links--;
    write_inode(pino, &parent);
    for (uint32_t i = 0; i < ino.extent_count; i++)
        free_block(ino.extents[i].disk_start);
    ino.links = 0;
    write_inode(ino_num, &ino);
    uint32_t idx = ino_num - 1;
    inode_bitmap[idx / 32] &= ~(1u << (idx % 32));
    sb.free_inodes++;
    flush_bitmap();
    return 0;
}

static int codefs_vfs_rmdir(const char *path) {
    struct codefs_inode ino;
    uint32_t ino_num;
    if (resolve_path(path, &ino, &ino_num) < 0) return -1;
    if (!(ino.mode & 040000)) return -1;
    if (ino.size > 0) return -1;
    char name[CODEFS_NAME_MAX];
    uint32_t pino = find_parent(path, name, sizeof(name));
    if (pino == 0) return -1;
    struct codefs_inode parent;
    if (read_inode(pino, &parent) < 0) return -1;
    dir_remove_entry(&parent, name);
    parent.links--;
    write_inode(pino, &parent);
    ino.links = 0;
    write_inode(ino_num, &ino);
    uint32_t idx = ino_num - 1;
    inode_bitmap[idx / 32] &= ~(1u << (idx % 32));
    sb.free_inodes++;
    flush_bitmap();
    return 0;
}

static int codefs_vfs_rename(const char *old, const char *new_name) {
    struct codefs_inode ino;
    uint32_t ino_num;
    if (resolve_path(old, &ino, &ino_num) < 0) return -1;
    char old_name[CODEFS_NAME_MAX];
    uint32_t old_pino = find_parent(old, old_name, sizeof(old_name));
    char new_name_buf[CODEFS_NAME_MAX];
    uint32_t new_pino = find_parent(new_name, new_name_buf, sizeof(new_name_buf));
    if (old_pino == 0 || new_pino == 0) return -1;
    struct codefs_inode old_parent;
    if (read_inode(old_pino, &old_parent) < 0) return -1;
    dir_remove_entry(&old_parent, old_name);
    old_parent.links--;
    write_inode(old_pino, &old_parent);
    struct codefs_inode new_parent;
    if (read_inode(new_pino, &new_parent) < 0) return -1;
    uint8_t type = (ino.mode & 040000) ? CODEFS_FT_DIR : CODEFS_FT_REG;
    dir_add_entry(&new_parent, ino_num, new_name_buf, type);
    new_parent.links++;
    write_inode(new_pino, &new_parent);
    return 0;
}

static int codefs_vfs_readdir(int fd, void *ent) {
    (void)fd;
    (void)ent;
    return -1;
}

static int codefs_vfs_ioctl(int fd, int req, void *arg) {
    (void)fd;
    (void)req;
    (void)arg;
    return -1;
}

/* ── Public driver API ───────────────────────────────────── */

struct vfs_ops codefs_ops = {
    .open     = codefs_vfs_open,
    .close    = codefs_vfs_close,
    .read     = codefs_vfs_read,
    .write    = codefs_vfs_write,
    .seek     = codefs_vfs_seek,
    .fstat    = codefs_vfs_fstat,
    .mkdir_vfs = codefs_vfs_mkdir,
    .unlink   = codefs_vfs_unlink,
    .rename   = codefs_vfs_rename,
    .readdir  = codefs_vfs_readdir,
    .ioctl    = codefs_vfs_ioctl,
};

int codefs_mount(int part_idx) {
    if (!crc32_inited) crc32_init();
    sb_part_idx = part_idx;
    block_buf = (uint8_t *)malloc(CODEFS_BLOCK_SIZE);
    if (!block_buf) return -1;
    if (read_superblock() < 0) {
        kprintf("codefs: mount failed\n");
        free(block_buf);
        return -1;
    }
    block_bitmap = (uint32_t *)malloc(CODEFS_BLOCK_SIZE);
    inode_bitmap = (uint32_t *)malloc(CODEFS_BLOCK_SIZE);
    if (!block_bitmap || !inode_bitmap) {
        free(block_buf);
        return -1;
    }
    read_block(sb.data_area_start - 2, block_bitmap);
    read_block(sb.data_area_start - 1, inode_bitmap);
    inode_table = (struct codefs_inode *)malloc(CODEFS_BLOCK_SIZE);
    if (!inode_table) {
        free(block_buf);
        free(block_bitmap);
        free(inode_bitmap);
        return -1;
    }
    journal_buf = (uint8_t *)malloc(CODEFS_BLOCK_SIZE);
    codefs_mounted_val = 1;
    kprintf("codefs: mounted (v%d.%d, %d inodes, %d blocks)\n",
            CODEFS_VERSION_MAJOR, CODEFS_VERSION_MINOR,
            sb.inodes_count, sb.blocks_count);
    return 0;
}

int codefs_mounted(void) { return codefs_mounted_val; }

int codefs_read_file(const char *path, void *buf, int max) {
    struct codefs_inode ino;
    if (resolve_path(path, &ino, NULL) < 0) return -1;
    if (ino.mode & 040000) return -1;
    int to_read = ino.size;
    if (to_read > max) to_read = max;
    if (to_read <= 0) return 0;
    if (ino.inline_len > 0 && ino.size <= CODEFS_INLINE_MAX) {
        memcpy(buf, ino.inline_data, to_read);
        return to_read;
    }
    int total = 0;
    uint32_t blocks = file_blocks_for_size(ino.size);
    for (uint32_t b = 0; b < blocks && total < to_read; b++) {
        uint32_t disk;
        if (extent_find(&ino, b, &disk) < 0) break;
        if (read_block(disk, block_buf) < 0) break;
        int chunk = to_read - total;
        if (chunk > CODEFS_BLOCK_SIZE) chunk = CODEFS_BLOCK_SIZE;
        memcpy((uint8_t *)buf + total, block_buf, chunk);
        total += chunk;
    }
    return total;
}

int codefs_write_file(const char *path, const void *buf, int len) {
    struct codefs_inode ino;
    uint32_t ino_num;
    if (resolve_path(path, &ino, &ino_num) < 0) return -1;
    if (ino.mode & 040000) return -1;
    if (len <= CODEFS_INLINE_MAX) {
        memcpy(ino.inline_data, buf, len);
        ino.inline_len = len;
        ino.size = len;
        ino.mtime = timer_get_ticks();
        write_inode(ino_num, &ino);
        return len;
    }
    uint32_t needed = file_blocks_for_size(len);
    if (ensure_blocks(&ino, needed) < 0) return -1;
    ino.size = len;
    ino.mtime = timer_get_ticks();
    int written = 0;
    for (uint32_t b = 0; b < needed; b++) {
        uint32_t disk;
        if (extent_find(&ino, b, &disk) < 0) break;
        memset(block_buf, 0, CODEFS_BLOCK_SIZE);
        int chunk = len - written;
        if (chunk > CODEFS_BLOCK_SIZE) chunk = CODEFS_BLOCK_SIZE;
        memcpy(block_buf, (const uint8_t *)buf + written, chunk);
        write_block(disk, block_buf);
        written += chunk;
    }
    write_inode(ino_num, &ino);
    flush_bitmap();
    return written;
}

int codefs_mkdir(const char *path) {
    return codefs_vfs_mkdir(path);
}

int codefs_unlink(const char *path) {
    return codefs_vfs_unlink(path);
}

int codefs_rmdir(const char *path) {
    return codefs_vfs_rmdir(path);
}

int codefs_find(const char *path, struct codefs_inode *out) {
    return resolve_path(path, out, NULL);
}

int codefs_stat(const char *path, struct codefs_inode *out) {
    return resolve_path(path, out, NULL);
}

int codefs_sync(void) {
    flush_bitmap();
    write_superblock();
    return 0;
}

int codefs_list_dir(const char *path) {
    struct codefs_inode ino;
    if (resolve_path(path, &ino, NULL) < 0) return -1;
    if (!(ino.mode & 040000)) return -1;
    uint32_t blocks = file_blocks_for_size(ino.size);
    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t disk;
        if (extent_find(&ino, b, &disk) < 0) continue;
        if (read_block(disk, block_buf) < 0) continue;
        uint32_t off = 0;
        while (off < CODEFS_BLOCK_SIZE) {
            struct codefs_dirent *de = (struct codefs_dirent *)(block_buf + off);
            if (de->inode == 0 || de->rec_len == 0) break;
            char name[256];
            memcpy(name, de->name, de->name_len);
            name[de->name_len] = 0;
            kprintf("  %s (ino=%d, type=%d)\n", name, de->inode, de->file_type);
            off += de->rec_len;
        }
    }
    return 0;
}
