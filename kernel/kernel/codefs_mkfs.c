/* codefs_mkfs.c — CodeFS (CPF) data-partition formatter for the installer.
 *
 * Produces a volume that the kernel's codefs.c mounts cleanly and pre-creates
 * the base tree (/, /etc, /home, and empty configuration files) because the
 * runtime's public write API (codefs_write_file) can only target files that
 * already exist — resolve_path() must succeed.  The installer then writes the
 * real config contents over the stubs.
 *
 * On-disk layout (matches codefs.c runtime EXACTLY — this is the format the
 * kernel mounts, NOT the stale scripts/mkcodefs.py):
 *   block 0:                     reserved (boot area)
 *   block 1:                     superblock at byte offset CODEFS_SB_OFFSET,
 *                                CRC32 over the full packed struct (crc=0)
 *   inode_table_start..+inode_table_blocks: inode table, 256-byte slots
 *   journal_start..+journal_len: journal
 *   data_area_start-2:           block bitmap (bit idx -> data_area_start+idx)
 *   data_area_start-1:           inode bitmap (bit idx -> inode idx+1)
 *   data_area_start..:           data blocks
 */

#include "codefs_mkfs.h"
#include "codefs.h"
#include "block.h"
#include "part.h"
#include "kprintf.h"
#include "string.h"
#include "mm.h"

#define CODEFS_INODE_TABLE_BLOCKS 256
#define CODEFS_INODES_PER_BLOCK   (CODEFS_BLOCK_SIZE / CODEFS_INODE_SIZE)

static struct codefs_superblock sb;

static uint32_t crc32_table[256];
static int crc32_inited;

static void crc32_init(void) {
    if (crc32_inited) return;
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

/* Write one 4K block at logical block number `blk` (0 = first block of the
 * data partition). */
static int blk_write(const partition_t *p, uint32_t blk, const void *data) {
    uint32_t lba = p->start_lba + (uint64_t)blk * CODEFS_BLOCK_SIZE / 512;
    if (block_write_sectors(lba, CODEFS_BLOCK_SIZE / 512, data) < 0) return -1;
    return 0;
}

int codefs_mkfs(int part_idx) {
    if (part_idx < 0) return -1;
    partition_t p;
    if (part_get(part_idx, &p) < 0) return -1;

    uint32_t total_blocks = (uint32_t)((uint64_t)p.sector_count * 512 / CODEFS_BLOCK_SIZE);
    uint32_t inode_table_start = 2;
    uint32_t inode_table_blocks = CODEFS_INODE_TABLE_BLOCKS;
    uint32_t journal_start = inode_table_start + inode_table_blocks;
    uint32_t journal_len = CODEFS_JOURNAL_BLOCKS;
    uint32_t data_area_start = journal_start + journal_len + 2; /* +2 bitmaps */
    if (total_blocks <= data_area_start) return -1;

    crc32_init();

    /* ── superblock ─────────────────────────────────────── */
    memset(&sb, 0, sizeof(sb));
    sb.magic = CODEFS_MAGIC;
    sb.version = CODEFS_VERSION;
    sb.flags = 0;
    sb.block_size = CODEFS_BLOCK_SIZE;
    sb.blocks_count = total_blocks;
    sb.free_blocks = total_blocks - data_area_start;   /* free data blocks */
    sb.inodes_count = 0;
    sb.free_inodes = sizeof(uint32_t) == 4 ? (inode_table_blocks * CODEFS_INODES_PER_BLOCK) : 0;
    sb.root_inode = 1;
    sb.journal_inode = 0;
    sb.journal_start = journal_start;
    sb.journal_len = journal_len;
    sb.inode_table_start = inode_table_start;
    sb.data_area_start = data_area_start;
    sb.inode_count = 0;
    sb.total_inodes = inode_table_blocks * CODEFS_INODES_PER_BLOCK;
    sb.mount_count = 0;
    sb.max_mounts = 0xFFFFFFFF;
    sb.state = CODEFS_S_CLEAN;
    sb.created_time = 0;
    sb.last_mount = 0;
    sb.last_write = 0;

    /* ── allocations (deterministic) ────────────────────── */
    uint8_t *block_bitmap = (uint8_t *)malloc(CODEFS_BLOCK_SIZE);
    uint8_t *inode_bitmap = (uint8_t *)malloc(CODEFS_BLOCK_SIZE);
    uint8_t *blockbuf = (uint8_t *)malloc(CODEFS_BLOCK_SIZE);
    uint8_t *blockbuf2 = (uint8_t *)malloc(CODEFS_BLOCK_SIZE);
    if (!block_bitmap || !inode_bitmap || !blockbuf || !blockbuf2) {
        free(block_bitmap); free(inode_bitmap); free(blockbuf); free(blockbuf2);
        return -1;
    }
    memset(block_bitmap, 0, CODEFS_BLOCK_SIZE);
    memset(inode_bitmap, 0, CODEFS_BLOCK_SIZE);

    /* Root dir data block = first data block; /etc block = second. */
    uint32_t root_blk = data_area_start + 0;
    uint32_t etc_blk  = data_area_start + 1;
    block_bitmap[0] |= 0x03;                 /* bits 0,1 -> both data blocks */
    memset(inode_bitmap, 0, CODEFS_BLOCK_SIZE);
    for (int i = 0; i < 6; i++)
        inode_bitmap[i / 8] |= (uint8_t)(1u << (i % 8));   /* inodes 1..6 */
    sb.free_blocks -= 2;
    sb.inodes_count = 6;
    sb.inode_count = 6;
    sb.free_inodes -= 6;

    /* ── inodes ─────────────────────────────────────────── */
    /* Inode slots are CODEFS_INODE_SIZE (256) bytes apart in the table even
     * though the packed struct is smaller — read_inode() seeks slot*256. */
    memset(blockbuf, 0, CODEFS_BLOCK_SIZE);

    /* ino 1: root directory */
    struct codefs_inode *root = (struct codefs_inode *)(blockbuf + 0 * CODEFS_INODE_SIZE);
    root->mode = 040755;
    root->uid = 0; root->gid = 0;
    root->size = CODEFS_BLOCK_SIZE;
    root->links = 3;
    root->extents[0].block = 0;
    root->extents[0].length = 1;
    root->extents[0].disk_start = root_blk;
    root->extent_count = 1;

    /* ino 2: /etc */
    struct codefs_inode *etc = (struct codefs_inode *)(blockbuf + 1 * CODEFS_INODE_SIZE);
    etc->mode = 040755;
    etc->size = CODEFS_BLOCK_SIZE;
    etc->links = 2;
    etc->extents[0].block = 0;
    etc->extents[0].length = 1;
    etc->extents[0].disk_start = etc_blk;
    etc->extent_count = 1;

    /* ino 3: /home (empty) */
    struct codefs_inode *home = (struct codefs_inode *)(blockbuf + 2 * CODEFS_INODE_SIZE);
    home->mode = 040755;
    home->size = 0;
    home->links = 2;

    /* inodes 4..6: empty config files in /etc */
    for (int i = 3; i < 6; i++) {
        struct codefs_inode *f = (struct codefs_inode *)(blockbuf + (uint32_t)i * CODEFS_INODE_SIZE);
        f->mode = 0100644;
        f->size = 0;
        f->links = 1;
    }

    if (blk_write(&p, inode_table_start, blockbuf) < 0) goto fail;

    /* ── directory blocks ───────────────────────────────── */
    struct codefs_dirent *de;

    /* root: ".", "..", "etc", "home" */
    memset(blockbuf, 0, CODEFS_BLOCK_SIZE);
    de = (struct codefs_dirent *)blockbuf;
    de->inode = 1;
    de->rec_len = (uint16_t)codefs_dir_rec_len(1);
    de->name_len = 1;
    de->file_type = CODEFS_FT_DIR;
    memcpy(de->name, ".", 1);
    uint32_t off = de->rec_len;

    de = (struct codefs_dirent *)(blockbuf + off);
    de->inode = 1;
    de->rec_len = (uint16_t)codefs_dir_rec_len(2);
    de->name_len = 2;
    de->file_type = CODEFS_FT_DIR;
    memcpy(de->name, "..", 2);
    off += de->rec_len;

    de = (struct codefs_dirent *)(blockbuf + off);
    de->inode = 2;
    de->rec_len = (uint16_t)codefs_dir_rec_len(3);
    de->name_len = 3;
    de->file_type = CODEFS_FT_DIR;
    memcpy(de->name, "etc", 3);
    off += de->rec_len;

    de = (struct codefs_dirent *)(blockbuf + off);
    de->inode = 3;
    de->rec_len = (uint16_t)codefs_dir_rec_len(4);
    de->name_len = 4;
    de->file_type = CODEFS_FT_DIR;
    memcpy(de->name, "home", 4);

    if (blk_write(&p, root_blk, blockbuf) < 0) goto fail;

    /* /etc: "users.conf", "wifi.conf", "locale.conf" */
    static const char *etc_files[3] = { "users.conf", "wifi.conf", "locale.conf" };
    memset(blockbuf, 0, CODEFS_BLOCK_SIZE);
    uint32_t roff = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t nlen = (uint32_t)strlen(etc_files[i]);
        de = (struct codefs_dirent *)(blockbuf + roff);
        de->inode = 4 + (uint32_t)i;
        de->rec_len = (uint16_t)codefs_dir_rec_len(nlen);
        de->name_len = (uint8_t)nlen;
        de->file_type = CODEFS_FT_REG;
        memcpy(de->name, etc_files[i], nlen);
        roff += de->rec_len;
    }
    if (blk_write(&p, etc_blk, blockbuf) < 0) goto fail;

    /* ── bitmaps + superblock ───────────────────────────── */
    if (blk_write(&p, data_area_start - 2, block_bitmap) < 0) goto fail;
    if (blk_write(&p, data_area_start - 1, inode_bitmap) < 0) goto fail;

    memset(blockbuf2, 0, CODEFS_BLOCK_SIZE);
    sb.crc = codefs_crc32(&sb, sizeof(sb));
    memcpy(blockbuf2 + (CODEFS_SB_OFFSET % CODEFS_BLOCK_SIZE), &sb, sizeof(sb));
    if (blk_write(&p, 1, blockbuf2) < 0) goto fail;

    free(block_bitmap); free(inode_bitmap); free(blockbuf); free(blockbuf2);
    kprintf("codefs: mkfs ok (%u blocks, %u inodes)\n", sb.blocks_count, sb.total_inodes);
    return 0;

fail:
    free(block_bitmap); free(inode_bitmap); free(blockbuf); free(blockbuf2);
    return -1;
}