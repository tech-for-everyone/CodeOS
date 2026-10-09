/* exfat_mkfs.c — ExFAT boot-partition formatter for the CodeOS installer.
 *
 * Mirrors the byte layout of a verified mkfs.exfat volume that boots through
 * the custom Limine stage2 (which reads ExFAT natively):
 *   - Boot region: s0 VBR, s1-8 extended (55AA at end of each), s9 OEM,
 *     s10 reserved, s11 checksum (one dword repeated across whole sector);
 *     backup boot region at sectors 12-23 (identical copy).
 *   - FAT1 at FatOffset (1MB boundary), heap at aligned offset, 32KB clusters.
 *   - Cluster 2 = allocation bitmap, 3 = upcase table, 4 = root directory,
 *     then two pseudo-partitions (boot/, limine/) + files, all NoFatChain
 *     and contiguous.  FAT entries for NoFatChain files stay 0.
 *   - Bitmap bit index = cluster - 2.
 *
 * Ported from the validated Python prototype (mkexfat_proto.py).  This is an
 * independent implementation derived from the public exFAT specification.
 */

#include "exfat_mkfs.h"
#include "exfat_upcase.h"
#include "block.h"
#include "part.h"
#include "kprintf.h"
#include "string.h"
#include "mm.h"

#define EXFAT_FIRST_CLUSTER     2
#define EXFAT_EOF_CLUSTER       0xFFFFFFFFu
#define EXFAT_CLUSTER           32768u
#define EXFAT_BOUNDARY          1048576u   /* 1MB */
#define EXFAT_SECTOR            512u
#define EXFAT_NUM_FATS          1u
#define EXFAT_UPCASE_SIZE       5836u
#define EXFAT_ROOT_CLUSTER      4u

/* Le helpers */
static void le16_put(void *p, uint16_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v & 0xFF); b[1] = (uint8_t)(v >> 8);
}
static void le32_put(void *p, uint32_t v) {
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v & 0xFF); b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF); b[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void le64_put(void *p, uint64_t v) {
    le32_put(p, (uint32_t)(v & 0xFFFFFFFFu));
    le32_put((uint8_t *)p + 4, (uint32_t)(v >> 32));
}

/* Sector write helper that splits >255-sector requests (block API cap). */
static int write_region(uint32_t start_lba, const uint8_t *data, uint32_t sectors) {
    uint32_t off = 0;
    while (off < sectors) {
        uint32_t n = sectors - off;
        if (n > 250) n = 250;
        if (block_write_sectors(start_lba + off, (uint8_t)n, data + off * 512) < 0)
            return -1;
        off += n;
    }
    return 0;
}

/* ── checksums ──────────────────────────────────────────────── */

/* Standard 32-bit boot-region checksum (exFAT spec 3.2.3).  Boot sectors skip
 * the absolute indices 106,107,112 (VBR pct/vol flags). */
static void boot_cksum(const uint8_t *data, uint32_t len, int is_boot_sec, uint32_t *c) {
    uint32_t acc = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (is_boot_sec && (i == 106 || i == 107 || i == 112))
            continue;
        acc = ((((acc & 1) << 31) | (acc >> 1)) + data[i]) & 0xFFFFFFFFu;
    }
    *c = acc;
}

/* Directory-entry-set checksum (16-bit), exFAT spec 7.4.1.2.  `primary` is
 * the FILE entry (skip bytes 2,3 = its checksum field); secondary entries use
 * all 32 bytes. */
static uint16_t dentry_set_cksum(const uint8_t *entries[], int n_entries) {
    uint16_t chk = 0;
    for (int idx = 0; idx < n_entries; idx++) {
        const uint8_t *d = entries[idx];
        /* byte 0, byte 1 */
        chk = (uint16_t)(((chk << 15) | (chk >> 1)) + d[0]);
        chk = (uint16_t)(((chk << 15) | (chk >> 1)) + d[1]);
        /* primary: skip 2,3 (its own checksum field).  secondary: all bytes,
         * so continue from 2 (0,1 already counted above). */
        int start = (idx == 0) ? 4 : 2;
        for (int i = start; i < 32; i++)
            chk = (uint16_t)(((chk << 15) | (chk >> 1)) + d[i]);
    }
    return chk;
}

/* exFAT name hash: 16-bit rotate-add over upcased UTF-16LE chars (low byte
 * then high byte of each upcased code unit).  Our names are ASCII so the
 * upcase table maps 'a'-'z' -> 'A'-'Z' and high bytes stay 0. */
static uint16_t name_hash(const char *name) {
    uint16_t chksum = 0;
    while (*name) {
        uint32_t u = (uint32_t)(uint8_t)*name++;
        if (u >= 'a' && u <= 'z') u -= 0x20;
        chksum = (uint16_t)(((chksum << 15) | (chksum >> 1)) + (u & 0xFF));
        chksum = (uint16_t)(((chksum << 15) | (chksum >> 1)) + (u >> 8));
    }
    return chksum;
}

/* Fixed DOS date/time (reproducible builds). */
static void dos_datetime(uint16_t *date, uint16_t *time) {
    uint16_t sec = 0, mn = 0, hr = 0, day = 1, mon = 1, year = 2025;
    *date = (uint16_t)((((year - 1980) & 0x7F) << 9) | (mon << 5) | day);
    *time = (uint16_t)((hr << 11) | (mn << 5) | (sec >> 1));
}

/* ── builder state ──────────────────────────────────────────── */

typedef struct {
    uint32_t fat_offset;    /* sectors */
    uint32_t fat_length;    /* sectors */
    uint32_t clu_offset;    /* sectors (heap start) */
    uint32_t clu_count;
    uint8_t  *fat_buf;      /* fat_length*512 bytes, flushed once */
    uint8_t  *bitmap;       /* cluster bitmap, bit = clu - 2 */
    uint32_t bitmap_bytes;
    uint32_t next_clu;
} exfat_builder_t;

static uint32_t round_up_u32(uint64_t v, uint64_t a) {
    return (uint32_t)((v + a - 1) & ~(a - 1));
}

static int b_init(exfat_builder_t *b, uint32_t vol_sectors) {
    b->fat_offset = round_up_u32(24u * EXFAT_SECTOR, EXFAT_BOUNDARY) / EXFAT_SECTOR;
    uint64_t max_clusters =
        ((uint64_t)vol_sectors * EXFAT_SECTOR - (uint64_t)b->fat_offset * EXFAT_SECTOR -
         EXFAT_NUM_FATS * 8 - 1) / (32768ull + 4ull * EXFAT_NUM_FATS) + 1;
    b->fat_length = round_up_u32((max_clusters + 2) * 4, EXFAT_SECTOR) / EXFAT_SECTOR;
    b->clu_offset = round_up_u32((uint64_t)b->fat_offset * EXFAT_SECTOR +
                                 (uint64_t)b->fat_length * EXFAT_SECTOR * EXFAT_NUM_FATS,
                                 EXFAT_BOUNDARY) / EXFAT_SECTOR;
    b->clu_count = (uint32_t)(((uint64_t)vol_sectors * EXFAT_SECTOR -
                               (uint64_t)b->clu_offset * EXFAT_SECTOR) / 32768ull);
    if (b->clu_count < 16 || b->fat_length < 4) return -1;

    b->bitmap_bytes = (b->clu_count + 7) / 8;
    b->next_clu = EXFAT_FIRST_CLUSTER;
    b->fat_buf = NULL;
    b->bitmap = NULL;
    return 0;
}

/* Allocate n contiguous clusters, mark them used in the in-memory bitmap. */
static uint32_t b_alloc(exfat_builder_t *b, uint32_t n) {
    uint32_t clu = b->next_clu;
    b->next_clu += n;
    for (uint32_t c = clu; c < clu + n; c++) {
        uint32_t idx = c - EXFAT_FIRST_CLUSTER;
        if (idx / 8 < b->bitmap_bytes)
            b->bitmap[idx / 8] |= (uint8_t)(1u << (idx % 8));
    }
    return clu;
}

/* LBA of the sector containing the start of cluster `c`. */
static uint32_t b_clu_lba(const exfat_builder_t *b, uint32_t c) {
    return b->clu_offset + (c - EXFAT_FIRST_CLUSTER) * (EXFAT_CLUSTER / EXFAT_SECTOR);
}

/* Patch cluster `c`'s FAT entry in the in-memory buffer. */
static void b_fat_set(exfat_builder_t *b, uint32_t c, uint32_t val) {
    le32_put(b->fat_buf + c * 4, val);
}

/* Write an entire cluster (already zero-padded to EXFAT_CLUSTER). */
static int b_write_clu(exfat_builder_t *b, uint32_t part_lba,
                       uint32_t clu, const uint8_t *data) {
    return write_region(part_lba + b_clu_lba(b, clu), data, EXFAT_CLUSTER / EXFAT_SECTOR);
}

/* ── dentry packing ─────────────────────────────────────────── */

static void pack_vol_label(uint8_t e[32]) {
    memset(e, 0, 32);
    e[0] = 0x83;              /* volume label, empty */
    e[1] = 0;
}

static void pack_guid_placeholder(uint8_t e[32]) {
    memset(e, 0, 32);
    e[0] = 0x20;              /* EXFAT_GUID & ~EXFAT_INVAL */
}

static void pack_bitmap_entry(uint8_t e[32], uint32_t start_clu, uint64_t size) {
    memset(e, 0, 32);
    e[0] = 0x81;
    le32_put(e + 20, start_clu);
    le64_put(e + 24, size);
}

static void pack_upcase_entry(uint8_t e[32], uint32_t checksum, uint32_t start_clu,
                              uint64_t size) {
    memset(e, 0, 32);
    e[0] = 0x82;
    le32_put(e + 4, checksum);
    le32_put(e + 20, start_clu);
    le64_put(e + 24, size);
}

/* FILE+STREAM+NAME entry set for a contiguous (NoFatChain) file/dir.
 * Names longer than 15 chars need multiple NAME entries.  Writes into out,
 * returns total byte length of the set (multiple of 32). */
static uint32_t brick_file_set(uint8_t *out, const char *name, int is_dir,
                               uint32_t start_clu, uint64_t size) {
    uint16_t date, time;
    dos_datetime(&date, &time);
    size_t nlen = strlen(name);
    uint32_t nn = (uint32_t)((nlen + 14) / 15);     /* NAME entry count */

    memset(out, 0, (1 + 1 + nn) * 32);

    uint8_t *file = out;
    uint8_t *stream = out + 32;
    file[0] = 0x85;
    file[1] = (uint8_t)(1 + nn);                    /* SetSecondaryCount */
    file[2] = 0x00; file[3] = 0x00;                 /* checksum patched below */
    file[4] = (uint8_t)(is_dir ? 0x10 : 0x20);      /* attr */
    le16_put(file + 8,  time); le16_put(file + 10, date);    /* create */
    le16_put(file + 12, time); le16_put(file + 14, date);    /* modify */
    le16_put(file + 16, time); le16_put(file + 18, date);    /* access */
    file[20] = 0x80; file[21] = 0x80; file[22] = 0x80;
    file[23] = 0x80; file[24] = 0x80;

    stream[0] = 0xC0;
    stream[1] = 0x03;                              /* AllocPossible | NoFatChain */
    stream[3] = (uint8_t)nlen;
    le16_put(stream + 4, name_hash(name));
    le64_put(stream + 8, size);                    /* valid data size */
    le32_put(stream + 20, start_clu);
    le64_put(stream + 24, size);

    for (uint32_t k = 0; k < nn; k++) {
        uint8_t *en = out + (2 + k) * 32;
        en[0] = 0xC1; en[1] = 0x00;
        size_t remain = nlen - (size_t)k * 15;
        size_t take = remain > 15 ? 15 : remain;
        for (size_t i = 0; i < take; i++) {
            uint8_t ch = (uint8_t)name[k * 15 + i];
            le16_put(en + 2 + i * 2, ch);
        }
    }

    /* Set entry-set checksum into FILE entry offset 2 */
    const uint8_t *set[3 + 1];
    for (uint32_t k = 0; k < 1 + 1 + nn; k++) set[k] = out + k * 32;
    le16_put(file + 2, dentry_set_cksum(set, 1 + 1 + nn));
    return (1 + 1 + nn) * 32;
}

/* ── boot region ────────────────────────────────────────────── */

static int write_boot_region(exfat_builder_t *b, uint32_t part_lba, uint32_t start_lba,
                             uint32_t vol_sectors, uint32_t vol_serial) {
    uint8_t boot[24 * EXFAT_SECTOR];
    memset(boot, 0, sizeof(boot));

    uint8_t *vbr = boot + 0;
    vbr[0] = 0xEB; vbr[1] = 0x76; vbr[2] = 0x90;
    memcpy(vbr + 3, "EXFAT   ", 8);
    le64_put(vbr + 64, start_lba);                 /* PartitionOffset */
    le64_put(vbr + 72, vol_sectors);               /* VolumeLength */
    le32_put(vbr + 80, b->fat_offset);
    le32_put(vbr + 84, b->fat_length);
    le32_put(vbr + 88, b->clu_offset);
    le32_put(vbr + 92, b->clu_count);
    le32_put(vbr + 96, EXFAT_ROOT_CLUSTER);
    le32_put(vbr + 100, vol_serial);
    vbr[104] = 0x00;                               /* FsRev minor */
    vbr[105] = 0x01;                               /* FsRev major */
    vbr[106] = 0x00; vbr[107] = 0x00;              /* VolumeFlags = 0 */
    vbr[108] = 9;                                  /* bps shift (512) */
    vbr[109] = 6;                                  /* spc shift (32K) */
    vbr[110] = EXFAT_NUM_FATS;
    vbr[111] = 0x80;                               /* drive no */
    vbr[112] = 0xFF;                               /* PercentInUse unknown */
    vbr[510] = 0x55; vbr[511] = 0xAA;

    /* Extended boot sectors 1-8: boot signature per sector */
    for (int s = 1; s <= 8; s++) {
        boot[s * EXFAT_SECTOR + 510] = 0x55;
        boot[s * EXFAT_SECTOR + 511] = 0xAA;
    }
    /* OEM parameters (s9) and reserved (s10) stay zeroed */

    /* Checksum sector (s11): one dword repeated across the whole sector */
    uint32_t chk = 0;
    boot_cksum(boot, 11 * EXFAT_SECTOR, 1, &chk);
    for (uint32_t i = 0; i < EXFAT_SECTOR; i += 4)
        le32_put(boot + 11 * EXFAT_SECTOR + i, chk);

    /* Backup boot region = identical copy of sectors 0-11 at sectors 12-23 */
    memcpy(boot + 12 * EXFAT_SECTOR, boot, 12 * EXFAT_SECTOR);

    /* Write main boot region (s0-11) + backup (s12-23) */
    return write_region(part_lba, boot, 24);
}

/* ── top-level format ───────────────────────────────────────── */

static int exfat_mkfs_internal(int part_idx,
                               const exfat_mkfs_file_t *boot_files, int n_boot,
                               const exfat_mkfs_file_t *limine_files, int n_limine) {
    partition_t p;
    if (part_get(part_idx, &p) < 0) {
        kprintf("exfat: no partition %d\n", part_idx);
        return -1;
    }
    uint32_t part_lba = p.start_lba;
    uint32_t vol_sectors = p.sector_count;
    if (vol_sectors < 64) return -1;

    uint32_t vol_serial = 0xFE9FF143u;             /* matches reference volume */

    exfat_builder_t b;
    if (b_init(&b, vol_sectors) < 0) {
        kprintf("exfat: volume too small\n");
        return -1;
    }
    b.fat_buf = (uint8_t *)malloc(b.fat_length * EXFAT_SECTOR);
    b.bitmap  = (uint8_t *)malloc(b.bitmap_bytes);
    if (!b.fat_buf || !b.bitmap) {
        kprintf("exfat: out of memory (%u fat + %u bitmap)\n",
                b.fat_length, b.bitmap_bytes);
        if (b.fat_buf) free(b.fat_buf);
        if (b.bitmap) free(b.bitmap);
        return -1;
    }
    memset(b.fat_buf, 0, b.fat_length * EXFAT_SECTOR);
    memset(b.bitmap, 0, b.bitmap_bytes);

    kprintf("exfat: fat_off=%u fat_len=%u clu_off=%u clu_count=%u\n",
            b.fat_offset, b.fat_length, b.clu_offset, b.clu_count);

    /* Boot region */
    if (write_boot_region(&b, part_lba, part_lba, vol_sectors, vol_serial) < 0) {
        kprintf("exfat: boot region write failed\n");
        return -1;
    }

    /* FAT init entries 0,1 */
    b_fat_set(&b, 0, 0xFFFFFFF8u);
    b_fat_set(&b, 1, 0xFFFFFFFFu);

    /* Metadata clusters */
    uint32_t bitmap_clu = b_alloc(&b, 1);
    uint32_t upcase_clu = b_alloc(&b, 1);
    uint32_t root_clu   = b_alloc(&b, 1);
    uint32_t boot_dir_clu  = b_alloc(&b, 1);
    uint32_t limine_dir_clu = b_alloc(&b, 1);

    /* Root directory contents */
    uint8_t root_cluster[EXFAT_CLUSTER];
    memset(root_cluster, 0, sizeof(root_cluster));
    uint32_t off = 0;

    pack_vol_label(root_cluster + off); off += 32;
    pack_guid_placeholder(root_cluster + off); off += 32;
    pack_bitmap_entry(root_cluster + off, bitmap_clu, b.bitmap_bytes); off += 32;

    uint32_t uc_chk = 0;
    boot_cksum(exfat_upcase_table, EXFAT_UPCASE_SIZE, 0, &uc_chk);
    pack_upcase_entry(root_cluster + off, uc_chk, upcase_clu, EXFAT_UPCASE_SIZE); off += 32;

    off += brick_file_set(root_cluster + off, "boot", 1, boot_dir_clu, EXFAT_CLUSTER);
    off += brick_file_set(root_cluster + off, "limine", 1, limine_dir_clu, EXFAT_CLUSTER);

    if (b_write_clu(&b, part_lba, root_clu, root_cluster) < 0) return -1;

    /* /boot contents */
    struct {
        const char *name;
        const void *data;
        uint64_t size;
        uint32_t clu;
    } boot_lut[8];
    if (n_boot > 8) n_boot = 8;
    memset(boot_lut, 0, sizeof(boot_lut));
    uint8_t dir_clu[EXFAT_CLUSTER];
    memset(dir_clu, 0, sizeof(dir_clu));
    off = 0;
    off = 0;
    for (int i = 0; i < n_boot; i++) {
        uint32_t nclu = (uint32_t)((boot_files[i].size + 32767u) / 32768u);
        if (nclu < 1) nclu = 1;
        boot_lut[i].name = boot_files[i].name;
        boot_lut[i].data = boot_files[i].data;
        boot_lut[i].size = boot_files[i].size;
        boot_lut[i].clu = b_alloc(&b, nclu);
    }
    for (int i = 0; i < n_boot; i++) {
        off += brick_file_set(dir_clu + off, boot_lut[i].name, 0,
                              boot_lut[i].clu, boot_lut[i].size);
        const uint8_t *src = (const uint8_t *)boot_lut[i].data;
        uint64_t remaining = boot_lut[i].size;
        uint32_t nclu = (uint32_t)((boot_lut[i].size + 32767u) / 32768u);
        if (nclu < 1) nclu = 1;
        for (uint32_t c = 0; c < nclu; c++) {
            uint8_t clu[EXFAT_CLUSTER];
            memset(clu, 0, sizeof(clu));
            uint64_t chunk = remaining < EXFAT_CLUSTER ? remaining : EXFAT_CLUSTER;
            if (chunk) memcpy(clu, src, (size_t)chunk);
            src += chunk;
            remaining -= chunk;
            if (b_write_clu(&b, part_lba, boot_lut[i].clu + c, clu) < 0) return -1;
        }
    }
    if (b_write_clu(&b, part_lba, boot_dir_clu, dir_clu) < 0) return -1;

    /* /limine contents */
    struct {
        const char *name;
        const void *data;
        uint64_t size;
        uint32_t clu;
    } lim_lut[4];
    if (n_limine > 4) n_limine = 4;
    memset(lim_lut, 0, sizeof(lim_lut));
    memset(dir_clu, 0, sizeof(dir_clu));
    off = 0;
    for (int i = 0; i < n_limine; i++) {
        uint32_t nclu = (uint32_t)((limine_files[i].size + 32767u) / 32768u);
        if (nclu < 1) nclu = 1;
        lim_lut[i].name = limine_files[i].name;
        lim_lut[i].data = limine_files[i].data;
        lim_lut[i].size = limine_files[i].size;
        lim_lut[i].clu = b_alloc(&b, nclu);
    }
    for (int i = 0; i < n_limine; i++) {
        off += brick_file_set(dir_clu + off, lim_lut[i].name, 0,
                              lim_lut[i].clu, lim_lut[i].size);
        const uint8_t *src = (const uint8_t *)lim_lut[i].data;
        uint64_t remaining = lim_lut[i].size;
        uint32_t nclu = (uint32_t)((lim_lut[i].size + 32767u) / 32768u);
        if (nclu < 1) nclu = 1;
        for (uint32_t c = 0; c < nclu; c++) {
            uint8_t clu[EXFAT_CLUSTER];
            memset(clu, 0, sizeof(clu));
            uint64_t chunk = remaining < EXFAT_CLUSTER ? remaining : EXFAT_CLUSTER;
            if (chunk) memcpy(clu, src, (size_t)chunk);
            src += chunk;
            remaining -= chunk;
            if (b_write_clu(&b, part_lba, lim_lut[i].clu + c, clu) < 0) return -1;
        }
    }
    if (b_write_clu(&b, part_lba, limine_dir_clu, dir_clu) < 0) return -1;

    /* FAT chains for metadata (single-cluster EOF).  NoFatChain files keep
     * their FAT entries at 0. */
    b_fat_set(&b, bitmap_clu, EXFAT_EOF_CLUSTER);
    b_fat_set(&b, upcase_clu, EXFAT_EOF_CLUSTER);
    b_fat_set(&b, root_clu, EXFAT_EOF_CLUSTER);

    /* Flush FAT */
    if (write_region(part_lba + b.fat_offset, b.fat_buf, b.fat_length) < 0) return -1;

    /* Bitmap + upcase clusters */
    uint8_t pad[EXFAT_CLUSTER];
    memset(pad, 0, sizeof(pad));
    memcpy(pad, b.bitmap, b.bitmap_bytes);
    if (b_write_clu(&b, part_lba, bitmap_clu, pad) < 0) return -1;
    memset(pad, 0, sizeof(pad));
    memcpy(pad, exfat_upcase_table, EXFAT_UPCASE_SIZE);
    if (b_write_clu(&b, part_lba, upcase_clu, pad) < 0) return -1;

    kprintf("exfat: ok (bitmap=%u upcase=%u root=%u /boot=%u /limine=%u next=%u)\n",
            bitmap_clu, upcase_clu, root_clu, boot_dir_clu, limine_dir_clu, b.next_clu);

    free(b.fat_buf);
    free(b.bitmap);
    return 0;
}

int exfat_mkfs_volume(int part_idx,
                      const exfat_mkfs_file_t *boot_files, int n_boot,
                      const exfat_mkfs_file_t *limine_files, int n_limine) {
    return exfat_mkfs_internal(part_idx, boot_files, n_boot, limine_files, n_limine);
}