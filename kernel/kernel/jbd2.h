#ifndef JBD2_H
#define JBD2_H

/* JBD2 on-disk structures, as used by ext3's journal.
 *
 * These are transcribed from e2fsprogs lib/ext2fs/kernel-jbd.h (a copy of the
 * kernel's include/linux/jbd.h) and cross-checked against the bytes mke2fs
 * actually writes, because the journal is read and written by e2fsck and
 * e2fsprogs as well as by us: a layout that is merely self-consistent produces
 * a filesystem that our driver likes and every real tool calls corrupt.
 *
 * Everything on disk is big-endian.
 *
 * ── Three traps worth stating up front, because all three are easy to get
 *    wrong and none of them fail loudly ──
 *
 * 1. The journal superblock's version lives in the h_blocktype field, and the
 *    constant for the modern layout is JBD2_SUPERBLOCK_V2 == 4. Reading
 *    "SUPERBLOCK_V2" as the number 2, or as 3, is the natural mistake: 3 is
 *    JBD2_SUPERBLOCK_V1, and 1/2 are DESCRIPTOR/COMMIT block types. The value
 *    on disk from mke2fs 1.47.x is 4.
 *
 * 2. In the superblock, s_sequence precedes s_start (0x18 then 0x1C), and they
 *    are easy to swap because the Linux kernel's own journal_superblock_s
 *    spells them h_start/h_sequence in the opposite conceptual order --
 *    s_sequence is "first commit ID expected in log", s_start is "blocknr of
 *    start of log". A fresh mke2fs journal has s_sequence == 1 and s_start == 0;
 *    that zero is the "log never used" marker, NOT a pointer at block 0, which
 *    is where the superblock itself lives. Writing to block 0 because of that
 *    reading destroys the journal superblock.
 *
 * 3. There is no standalone 8-byte "tag1". The descriptor tag is one struct,
 *    journal_block_tag_s, TRUNCATED to a feature-dependent length:
 *
 *        blocknr(4) checksum(2) flags(2) blocknr_high(4)   = 12 bytes
 *
 *    and journal_tag_bytes() shortens it as follows:
 *
 *        csum_v3 set            -> 16 bytes (a different struct, tag3)
 *        64bit set              -> 12 bytes (or 14 with csum_v2)
 *        csum_v2 set            -> 10 bytes
 *        none of the above      ->  8 bytes   <-- what mke2fs -t ext3 writes
 *
 *    So in the default 8-byte form, t_flags is a *16-bit* field at offset 6 and
 *    the two bytes at offset 4 are an unused checksum slot. Treating flags as a
 *    32-bit word (the obvious reading of "8 bytes = 2 u32") puts the flag bits
 *    in the wrong half of the field.
 *
 * Tags start at 4-byte-aligned offsets within the descriptor block; with the
 * default 8-byte tags that is a no-op, but it must be applied for the 10- and
 * 12-byte forms, where it is not.
 */

#include "types.h"

/* ── journal block types (journal_block_type_t) ──────────────────────── */
#define JBD2_DESCRIPTOR_BLOCK  1
#define JBD2_COMMIT_BLOCK      2
#define JBD2_SUPERBLOCK_V1     3
#define JBD2_SUPERBLOCK_V2     4
#define JBD2_REVOKE_BLOCK      5
#define JBD2_FC_BLOCK          6

/* Magic: in the journal superblock, in every commit block, and in the pad
 * words following the data blocks of the final descriptor in a transaction. */
#define JBD2_MAGIC_NUMBER      0xC03B3998U

/* Reserved for the journal superblock at the head of the journal inode. */
#define JBD2_HEADER_SIZE       1024
#define JBD2_MIN_BLOCK_SIZE    1024
#define JBD2_MAX_BLOCK_SIZE    32768

/* Cap on the journal's pre-resolved block map, in entries. 65536 blocks is
 * 256 MiB of journal, far past what anyone mounts, and the map costs 4 bytes
 * per entry -- a 4 MiB journal on a 1 KiB-block filesystem needs 4096. Past
 * this the driver simply walks the inode on demand instead (see jphys()), so
 * an outsized journal is slower, never wrong. */
#define JBD2_MAX_MAP           65536

/* ── s_checksum_type ──────────────────────────────────────────────────── */
#define JBD2_CRC32_CHKSUM      1
#define JBD2_MD5_CHKSUM        2
#define JBD2_SHA1_CHKSUM       3
#define JBD2_CRC32C_CHKSUM     4

#define JBD2_CHECKSUM_BYTES    8   /* __be32 h_chksum[8] in a commit block */

/* ── feature bits ────────────────────────────────────────────────────── */
#define JBD2_FEATURE_COMPAT_CHECKSUM       0x00000001

#define JBD2_FEATURE_INCOMPAT_REVOKE       0x00000001
#define JBD2_FEATURE_INCOMPAT_64BIT        0x00000002
#define JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT 0x00000004
#define JBD2_FEATURE_INCOMPAT_CSUM_V2      0x00000008
#define JBD2_FEATURE_INCOMPAT_CSUM_V3      0x00000010
#define JBD2_FEATURE_INCOMPAT_FAST_COMMIT  0x00000020

/* ── descriptor block tag ────────────────────────────────────────────── */
/* Layout: blocknr(4) t_checksum(2) t_flags(2) t_blocknr_high(4) = 12 bytes.
 * Truncated per journal_tag_bytes() to 16/14/12/10/8 as described above. */
struct jbd2_tag {
    uint32_t t_blocknr;
    uint16_t t_checksum;    /* meaningful only with csum_v2 */
    uint16_t t_flags;
    uint32_t t_blocknr_high;/* only read when the 64bit feature is set */
} __attribute__((packed));

/* The csum_v3 form is a genuinely different struct -- note the field order,
 * t_flags before t_blocknr_high, and a full 32-bit checksum last. */
struct jbd2_tag3 {
    uint32_t t_blocknr;
    uint32_t t_flags;
    uint32_t t_blocknr_high;
    uint32_t t_checksum;
} __attribute__((packed));

#define JBD2_TAG_SIZE_DEFAULT  8   /* no features: blocknr+cksum+flags */
#define JBD2_TAG_SIZE_CSUM_V2  10
#define JBD2_TAG_SIZE_64BIT    12
#define JBD2_TAG_SIZE_64_CSUM2 14
#define JBD2_TAG_SIZE_CSUM_V3  16

/* t_flags bit assignments (JBD2_FLAG_*). */
#define JBD2_FLAG_ESCAPE     1  /* never on disk: marks an unused tag slot */
#define JBD2_FLAG_SAME_UUID  2  /* this journal's uuid == the fs's uuid */
#define JBD2_FLAG_DELETED    4  /* entry deleted; skip it during replay */
#define JBD2_FLAG_LAST_TAG   8  /* end of the tag list in this descriptor */
#define JBD2_FLAG_MASK       0x0F

/* ── common header on every descriptor / commit / revoke block ────────── */
struct jbd2_header {
    uint32_t h_magic;      /* JBD2_MAGIC_NUMBER */
    uint32_t h_blocktype;
    uint32_t h_sequence;
} __attribute__((packed));

/* ── commit block ────────────────────────────────────────────────────── */
/* The three fields we must read for replay sit at offsets 0/4/8. The rest is
 * used only by checksum-v1 journals; we zero-fill it, which is what a v1-less
 * journal expects. */
struct jbd2_commit {
    struct jbd2_header h;          /* 12 bytes */
    uint8_t  h_chksum_type;
    uint8_t  h_chksum_size;
    uint8_t  h_padding[2];
    uint32_t h_chksum[JBD2_CHECKSUM_BYTES];
    uint64_t h_commit_sec;
    uint32_t h_commit_nsec;
} __attribute__((packed));

/* ── revoke table ────────────────────────────────────────────────────── */
/* A revoke block starts with a journal header plus a byte count, followed by a
 * bare bitmap: word N covers blocks 32*N .. 32*N+31. The bitmap follows the
 * last tag of a descriptor block, 4-byte aligned, and is sized for the highest
 * revoked block: ((highest / 32) + 1) * 4 bytes. */
struct jbd2_revoke_header {
    struct jbd2_header r_header;
    uint32_t r_count;      /* bytes of bitmap that follow */
} __attribute__((packed));

#define JBD2_REVOKE_SIZE(highest_block) ((((highest_block) / 32) + 1) * 4)

/* ── driver API (jbd2.c) ───────────────────────────────────────────────── */

/* Load the journal superblock and replay any committed transactions. Returns 0
 * on success. Returns -1 -- deliberately, so the caller can fall back to
 * journalless ext2 -- when there is no journal, when it needs a tag layout this
 * driver does not implement, or when buffers cannot be allocated. */
int  jbd2_init(uint32_t journal_inode);

/* Record the log position and release the journal. Call on unmount, and on the
 * remount path, so the next boot starts from a known point. */
void jbd2_shutdown(int clean);

/* Transaction boundaries. These nest: only the outermost begin/commit pair owns
 * the journal write, so internal helpers need not know whether they are being
 * called inside a larger operation. */
int  jbd2_txn_begin(void);
int  jbd2_txn_commit(void);

/* Stage a block for journalling. Returns non-zero if the caller must also write
 * the block itself, which is the case when no journal is present. Returns 0 when
 * the block has been staged and the journal will place it. */
int  jbd2_stage_block(uint32_t fs_block, const void *data);

/* Tell the journal a block is being freed, so a transaction that also writes it
 * does not resurrect the old contents on replay. */
void jbd2_note_free(uint32_t fs_block);

/* Read-your-writes for a staged block.
 *
 * Returns 0 and fills `buf` (which must have room for j_blocksize) when
 * `fs_block` has a version pending in the open transaction, and -1 when it does
 * not, in which case the caller should read from the disk as usual.
 *
 * This is not an optimisation. A journalled write is not on disk until the
 * transaction commits, so without it a metadata block modified earlier in the
 * same transaction reads back stale, and the failure is a *plausible* wrong
 * answer rather than an error:
 *
 *   - the block allocator re-reads the block bitmap to find a free block, gets
 *     the pre-allocation version, and hands the same block to every allocation
 *     in the transaction. A 20-block file then gets one block, thirteen times,
 *     in its inode -- which e2fsck reports as "illegal block" because it read
 *     file data out of it and took that for a block pointer.
 *   - the inode allocator does the same to the inode bitmap.
 *   - a directory that grows within one transaction re-reads itself stale.
 *
 * All of them are fixed by one call at the top of the filesystem driver's
 * block read, which is why it lives here rather than being sprinkled through
 * the allocator.
 *
 * Only the newest stage of a block is returned: staging the same block twice in
 * one transaction overwrites its earlier copy, and a later stage is always the
 * newer content, so the scan runs backwards.
 */
int  jbd2_peek_block(uint32_t fs_block, void *buf);

int  jbd2_have_journal(void);
int  jbd2_in_transaction(void);

#endif /* JBD2_H */
