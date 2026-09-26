#include "jbd2.h"
#include "ext2.h"
#include "kprintf.h"
#include "string.h"
#include "mm.h"

/* JBD2 journal for the ext2/3 driver.
 *
 * Implements the 8-byte tag layout and the csum_v2 10-byte layout
 * (blocknr + crc16 + flags). The journal is real JBD2: descriptors,
 * data blocks, commit blocks, and revoke blocks are all written and
 * replayed. Checksums (CRC32 for the commit block, CRC16 for data
 * blocks) are computed on write and verified on replay when the
 * JBD2_FEATURE_INCOMPAT_CSUM_V2 feature is set.
 *
 * ── The write ordering is the whole difficulty ──
 *
 * Getting this wrong does not fail loudly. It produces a filesystem that passes
 * every functional test and is quietly corrupt after a power cut. The only
 * ordering that is safe is:
 *
 *     1. write the journal descriptor block(s)   (the tag list)
 *     2. write the journal data blocks           (new content of each tag)
 *     3. write the journal commit block          <-- THE COMMIT POINT
 *     4. only now write the blocks to their home locations
 *
 * Step 4 must follow step 3. If the home location went first and we crashed
 * before the commit block landed, replay would correctly discard the
 * transaction while the home location already held half of it -- an operation
 * applied in part, which is exactly what a journal exists to prevent.
 *
 * Step 2 needs the new content of every tagged block, and step 2 happens at
 * commit time rather than at the call, so that content must survive until
 * commit. Hence the staging buffer: each tagged block's new bytes are copied in
 * as it is tagged, and when the staging area fills the transaction commits and
 * a new one starts. That bounds memory instead of requiring a whole operation's
 * worth of RAM up front, and matches what a real journaling filesystem does
 * when its cache cannot hold an entire operation. The cost is that a large
 * write is split across transactions, so a crash mid-write can leave a file
 * shorter than intended -- consistent, but not atomic. ext3 offers the same
 * guarantee, so this is not a regression.
 *
 * ── What gets journaled ──
 *
 * Everything. ext2.c funnels every block write -- data, bitmaps, inodes,
 * directory blocks, group descriptors -- through one function, and that single
 * choke point is hooked in ext2.c's write_block(). Splitting writes into
 * "metadata" and "data" so that only metadata is journaled would be closer to
 * ext3's default ordered mode, but it requires auditing every write path for a
 * missed case, and a missed case is silent corruption. Journalling data blocks
 * as well is journalled-data mode, which ext3 permits and e2fsck accepts; it
 * costs journal space and buys certainty that no path is unjournalled. The
 * superblock is the deliberate exception: it is the anchor recovery trusts, so
 * it is written directly and never staged.
 */
/* ── byte order ────────────────────────────────────────────────────────────
 * ext2 fields are little-endian on disk, so ext2.c reads them raw and is correct
 * on x86 by construction. JBD2 is the opposite: every field is big-endian
 * regardless of host, so every access here must swap. Reading JBD2 raw on a
 * little-endian host yields a plausible-looking but entirely wrong magic,
 * blocksize and sequence -- so these are not optional.
 */
static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}
#define be32(v) bswap32(v)
#define le32(v) bswap32(v)
static inline uint16_t bswap16(uint16_t v) {
    return (uint16_t)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}
#define be16(v) bswap16(v)
#define le16(v) bswap16(v)

/* ── CRC32 (IEEE 802.3, reflected) for commit-block checksums ──────────
 * and CRC16-CCITT (XMODEM) for data-block tag checksums (csum_v2).
 * Both are self-contained: no external dependency. */
static uint32_t crc32_tab[256];
static int crc32_ready = 0;

static void init_crc32(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0);
        crc32_tab[i] = crc;
    }
    crc32_ready = 1;
}

static uint32_t crc32(const uint8_t *data, uint32_t len) {
    if (!crc32_ready) init_crc32();
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_tab[(crc ^ data[i]) & 0xFF];
    return crc ^ 0xFFFFFFFFu;
}

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len) {
    uint16_t crc = 0x0000;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0);
    }
    return crc;
}

/* ── tunables ────────────────────────────────────────────────────────────
 * STAGE_BLOCKS bounds journal memory (64 KiB with 1 KiB journal blocks, 256 KiB
 * with 4 KiB). It affects only how often a large write is split across
 * transactions, never correctness.
 */
#define JBD2_STAGE_BLOCKS 64
#define JBD2_MAX_TAGS     JBD2_STAGE_BLOCKS

/* Refuse to start a transaction that would come within this many blocks of the
 * end of the journal, so a transaction never straddles the wrap point. */
#define JBD2_WRAP_MARGIN  2

/* Descriptor block overhead: the 12-byte header, plus room for the two tags a
 * block must always be able to hold (the last one carries LAST_TAG). Used to
 * decide whether a revoke table still fits. */
#define JBD2_DESC_OVERHEAD (sizeof(struct jbd2_header) + 2 * JBD2_TAG_SIZE_DEFAULT)

static int      j_present;
static uint32_t j_inum;

static uint32_t j_blocksize;
static uint32_t j_maxlen;
static uint32_t j_first;
static uint32_t j_head;        /* offset from 0 of the next free journal block */
static uint32_t j_sequence;    /* sequence of the next transaction to write */
static uint32_t j_seq_next;    /* sequence handed to the transaction in flight */

static int      j_tag_bytes;
static uint32_t j_feature_incompat;

/* Revoke table for the transaction in flight.
 *
 * Sized from the FILESYSTEM block count, not the journal length: the bitmap
 * indexes the blocks being revoked, which are ordinary fs blocks and routinely
 * far outnumber the journal's own 4096 blocks. Sizing it from j_maxlen would
 * silently drop revokes for any block past the journal length -- the exact case
 * where a resurrected block does the most damage. */
static uint8_t *j_revoke_bits;
static uint32_t j_revoke_bytes;   /* allocated size of j_revoke_bits */
static uint32_t j_revoke_hwm;     /* highest block revoked this transaction */

/* staging area + tag table for the transaction in flight */
static uint8_t  *j_stage;
static uint32_t  j_tag_block[JBD2_MAX_TAGS];
static uint32_t  j_tag_slot[JBD2_MAX_TAGS];
static int       j_ntags;
static int       j_txn_depth;

static uint8_t  *j_iobuf;      /* scratch for reading journal blocks */
static uint8_t  *j_descbuf;    /* scratch for building descriptor/commit blocks */
static uint8_t  *j_sb;         /* the 1024-byte journal superblock */
static uint8_t  *j_probe;      /* full-block scratch used before j_blocksize is
                                 * known; see jbd2_load_sb() */
static uint8_t  *j_save;       /* holds a staged block across a mid-transaction
                                 * flush; see jbd2_stage_block() */

static int jbd2_replay(void);
static int jbd2_wrap(void);
static int jbd2_flush(void);
static int jbd2_build_map(void);

/* ── journal block addressing ────────────────────────────────────────────
 * The journal is an ordinary file, so journal block N is whatever fs block the
 * journal inode's block map says for index N. Going through the inode rather
 * than assuming contiguity matters: mke2fs happens to lay a journal out
 * contiguously, but nothing guarantees it, and a 4 MiB journal in a 4 KiB-block
 * filesystem needs an indirect block after the first twelve.
 *
 * The map is resolved once, at mount, into j_map. It used to be re-walked on
 * every single journal block access, and that walk runs in the filesystem
 * driver's shared block buffer (read_inode_block is an ext2.c function with no
 * buffer of its own). That was a live corruption bug, not just a slow path: a
 * flush in the middle of staging left the journal inode's indirect block sitting
 * in that shared buffer, and the next staged block was copied from it -- so the
 * journal faithfully wrote a block of block-numbers over the caller's target. On
 * a 1 KiB-block filesystem that landed the journal's indirect block (a run of
 * consecutive block numbers) on top of the group descriptor table.
 *
 * The journal inode is never rewritten while the journal is mounted, so the map
 * cannot change under us and there is nothing to invalidate.
 */
static uint32_t *j_map;
static uint32_t  j_map_len;

static int jphys(uint32_t jblock, uint32_t *out) {
    if (j_map) {
        if (jblock >= j_map_len || !j_map[jblock]) return -1;
        *out = j_map[jblock];
        return 0;
    }
    /* Only reachable while the journal superblock is being read: j_maxlen is
     * what sizes the map, and that is not known until the superblock is in hand.
     * Nothing holds block_buf across those four reads at mount time. */
    uint32_t p = ext2_inode_phys_block((int)j_inum, (int)jblock);
    if (!p) return -1;
    *out = p;
    return 0;
}

/* Resolve the journal's whole block map. A failure here is not fatal: jphys()
 * falls back to walking the inode, which is correct but slow. */
static int jbd2_build_map(void) {
    if (j_maxlen == 0 || j_maxlen > JBD2_MAX_MAP) return -1;
    uint32_t *m = (uint32_t *)malloc(j_maxlen * sizeof(uint32_t));
    if (!m) return -1;
    for (uint32_t i = 0; i < j_maxlen; i++)
        m[i] = ext2_inode_phys_block((int)j_inum, (int)i);
    j_map = m;
    j_map_len = j_maxlen;
    return 0;
}

/* Read one journal block into j_probe.
 *
 * j_probe is always JBD2_MAX_BLOCK_SIZE, which exceeds any legal journal block,
 * so there is no destination a caller can get wrong. Callers copy out what they
 * need (see jread_hdr / jread_block).
 *
 * This previously took a caller-supplied destination, and every call that passed
 * `&some_struct_jbd2_header` wrote a whole 1 KiB block over 12 bytes of stack --
 * which silently smashed the return address and faulted with RIP=0.
 *
 * There is deliberately no `jblock < j_maxlen` check here: j_maxlen is zero
 * until the superblock has been read, so bounding on it would fail every read in
 * the load path. The replay walk checks the bound itself before calling.
 */
static int jprobe(uint32_t jblock) {
    uint32_t phys;
    if (jphys(jblock, &phys) < 0) return -1;
    return ext2_read_block_from(phys, j_probe);
}

/* Copy out just the 12-byte header. `out` is the only caller-sized buffer in
 * the read path, and it is exactly the size of what is copied. */
static int jread_hdr(uint32_t jblock, struct jbd2_header *out) {
    if (jprobe(jblock) < 0) return -1;
    memcpy(out, j_probe, sizeof(*out));
    return 0;
}

/* Copy out a whole block. `dst` must hold at least j_blocksize bytes, which is
 * true of j_iobuf and j_sb by construction. */
static int jread_block(uint32_t jblock, void *dst) {
    if (jprobe(jblock) < 0) return -1;
    memcpy(dst, j_probe, j_blocksize);
    return 0;
}

static int jwrite(uint32_t jblock, const void *src) {
    uint32_t phys;
    if (jphys(jblock, &phys) < 0) return -1;
    return ext2_write_block_from(phys, src);
}

/* ── superblock ──────────────────────────────────────────────────────────
 * The journal superblock is 1024 bytes in the journal inode's first block.
 * Redundant copies follow; we read them in order and keep the highest sequence,
 * which is how we recover if the write of the superblock itself was torn.
 */
static int jbd2_load_sb(void) {
    uint32_t best = 0;
    int have = 0, found = -1;

    for (uint32_t i = 0; i < 4; i++) {
        struct jbd2_header h;
        if (jread_hdr(i, &h) < 0) continue;
        if (be32(h.h_magic) != JBD2_MAGIC_NUMBER) continue;
        if (be32(h.h_blocktype) != JBD2_SUPERBLOCK_V2) continue;
        uint32_t seq = be32(h.h_sequence);
        if (!have || seq > best) { best = seq; have = 1; found = (int)i; }
    }
    if (found < 0) {
        /* Print what is actually there. "no readable journal superblock" is not
         * an actionable diagnosis, and the three ways this can go wrong --
         * wrong inode, wrong endianness, wrong offset -- look identical from
         * the outside. */
        struct jbd2_header d;
        if (jread_hdr(0, &d) == 0)
            kprintf("ext3: inode %u block 0: magic 0x%08x type %u seq %u"
                    " (want magic 0x%08x type %u)\n",
                    j_inum, be32(d.h_magic), be32(d.h_blocktype),
                    be32(d.h_sequence), JBD2_MAGIC_NUMBER, JBD2_SUPERBLOCK_V2);
        else
            kprintf("ext3: inode %u block 0 is unreadable\n", j_inum);
        return -1;
    }

    /* The journal superblock is the 1024 bytes at the head of its block. The
     * block itself may be larger, so copy just the superblock and leave j_sb
     * sized to exactly that. */
    if (jprobe((uint32_t)found) < 0) return -1;
    memset(j_sb, 0, JBD2_HEADER_SIZE);
    memcpy(j_sb, j_probe, JBD2_HEADER_SIZE);

    j_blocksize        = be32(*(uint32_t *)(j_sb + 12));  /* s_blocksize */
    j_maxlen           = be32(*(uint32_t *)(j_sb + 16));  /* s_maxlen */
    j_first            = be32(*(uint32_t *)(j_sb + 20));  /* s_first */
    j_sequence         = be32(*(uint32_t *)(j_sb + 24));  /* s_sequence: first
                                                           * commit ID expected */
    uint32_t s_start   = be32(*(uint32_t *)(j_sb + 28));  /* s_start: blocknr of
                                                           * start of log */
    j_feature_incompat = be32(*(uint32_t *)(j_sb + 40));

    if (j_blocksize < JBD2_MIN_BLOCK_SIZE || j_blocksize > JBD2_MAX_BLOCK_SIZE)
        return -1;
    if (j_maxlen <= j_first) return -1;

    /* journal_tag_bytes(), transcribed. The tag is one struct truncated to a
     * feature-dependent length, so the no-feature case is 12-4 = 8 bytes. */
    if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_CSUM_V3) {
        j_tag_bytes = JBD2_TAG_SIZE_CSUM_V3;
    } else {
        int sz = JBD2_TAG_SIZE_64BIT;                 /* 12 */
        if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_CSUM_V2) sz += 2;  /* 14 */
        if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_64BIT) j_tag_bytes = sz;
        else j_tag_bytes = sz - 4;                    /* 10, or 8 with no csum */
    }

    /* s_start == 0 is mke2fs's "log never used" marker, not a block number:
     * block 0 of the journal is the superblock itself, and writing there because
     * of that reading destroys the journal. Normalise to the first log block. */
    if (s_start < j_first || s_start >= j_maxlen) s_start = j_first;

    j_head = s_start;
    return 0;
}

static int jbd2_store_sb(uint32_t s_start, uint32_t s_sequence) {
    *(uint32_t *)(j_sb + 12) = le32(j_blocksize);
    *(uint32_t *)(j_sb + 16) = le32(j_maxlen);
    *(uint32_t *)(j_sb + 20) = le32(j_first);
    *(uint32_t *)(j_sb + 24) = le32(s_sequence);
    *(uint32_t *)(j_sb + 28) = le32(s_start);

    /* Write a whole block, not just the 1024-byte superblock. If the journal
     * block size exceeds JBD2_HEADER_SIZE, writing only the superblock would
     * leave the tail of the block as it was, and ext2_write_block_from moves
     * block_size bytes -- so the tail would be read straight back out of
     * whatever buffer happened to follow. Pad through j_probe. */
    memset(j_probe, 0, j_blocksize);
    memcpy(j_probe, j_sb, JBD2_HEADER_SIZE);
    return jwrite(0, j_probe);
}

/* ── transactions ─────────────────────────────────────────────────────── */

static void jbd2_reset_txn(void) {
    j_ntags = 0;
    j_revoke_hwm = 0;
    if (j_revoke_bits) memset(j_revoke_bits, 0, j_revoke_bytes);
}

static int jbd2_is_revoked(uint32_t b) {
    if (!j_revoke_bits || b > j_revoke_hwm) return 0;
    return (j_revoke_bits[b / 8] >> (b % 8)) & 1;
}

/* Record that a block was freed inside the current transaction. Replaying the
 * transaction would otherwise resurrect the old contents into a block that has
 * since been reallocated, so the tag is dropped and the block named in the
 * revoke table.
 *
 * The table is a bitmap covering blocks 0..hwm, so revoking a high block makes
 * it big. If it would not fit in one descriptor block we commit first, so the
 * free lands in a transaction of its own. That costs an extra transaction
 * boundary and is the reason the boundary is worth having.
 */
static int jbd2_revoke(uint32_t fs_block) {
    if (!j_present || j_txn_depth == 0) return 0;

    uint32_t hwm = fs_block > j_revoke_hwm ? fs_block : j_revoke_hwm;
    if (JBD2_REVOKE_SIZE(hwm) + JBD2_DESC_OVERHEAD > j_blocksize) {
        /* The table would not fit in one descriptor block. End the transaction
         * so the free lands in one of its own; that costs a transaction
         * boundary, which is why having boundaries is worth the trouble. */
        int depth = j_txn_depth;
        if (jbd2_flush() < 0) return -1;
        j_txn_depth = depth;
        jbd2_reset_txn();
        j_seq_next = j_sequence;
    }

    if (fs_block / 8 >= j_revoke_bytes) return 0;   /* outside our bitmap */
    j_revoke_bits[fs_block / 8] |= (uint8_t)(1u << (fs_block % 8));
    if (fs_block > j_revoke_hwm) j_revoke_hwm = fs_block;

    for (int i = 0; i < j_ntags; i++) {
        if (j_tag_block[i] == fs_block) {
            j_tag_block[i] = j_tag_block[--j_ntags];
            j_tag_slot[i]  = j_tag_slot[j_ntags];
            i--;
        }
    }
    return 0;
}

/* Blocks a transaction of n tags occupies: its descriptor blocks, its data
 * blocks, and the commit block. */
static int jbd2_txn_blocks(int ntags) {
    int per_desc = (int)(j_blocksize - sizeof(struct jbd2_header)) / j_tag_bytes;
    if (per_desc < 1) return 1 << 30;      /* pathological: no tag fits at all */
    int ndesc = (ntags + per_desc - 1) / per_desc;
    if (ndesc < 1) ndesc = 1;
    return ndesc + ntags + 1;
}

/* Move the log back to the first log block for a new epoch.
 *
 * The journal superblock MUST be updated before the first block of the new
 * epoch is written. If we crash after overwriting but before recording the new
 * start and sequence, recovery would begin at the old start expecting the old
 * sequence, meet the new epoch's higher sequence at once, and silently skip
 * every transaction written since the wrap.
 */
static int jbd2_wrap(void) {
    j_head = j_first;
    return jbd2_store_sb(j_first, j_sequence);
}

int jbd2_txn_begin(void) {
    if (!j_present) return -1;
    if (j_txn_depth++ > 0) return 0;      /* nested: outermost owns the commit */

    /* Keep the largest possible transaction clear of the end of the journal, so
     * it cannot straddle the wrap point: replay walks the log linearly and would
     * read the tail of one transaction as the head of the next. */
    if (j_head + (uint32_t)jbd2_txn_blocks(JBD2_MAX_TAGS) + JBD2_WRAP_MARGIN
        >= j_maxlen) {
        /* Flush directly rather than through jbd2_txn_commit(), which would
         * decrement the depth this very function just incremented. */
        if (jbd2_flush() < 0) { j_txn_depth = 0; return -1; }
        if (jbd2_wrap() < 0) { j_txn_depth = 0; return -1; }
    }

    jbd2_reset_txn();
    j_seq_next = j_sequence;
    return 0;
}

int jbd2_txn_commit(void) {
    if (!j_present) return -1;
    if (j_txn_depth > 0 && --j_txn_depth > 0) return 0;
    if (j_txn_depth < 0) j_txn_depth = 0;
    return jbd2_flush();
}

/* Write the staged blocks out as one transaction.
 *
 * This does no depth bookkeeping, so it is reachable both from the public
 * commit above and from the two places that must end a transaction early: the
 * staging area filling up, and a revoke table that would not fit in a
 * descriptor block.
 */
static int jbd2_flush(void) {
    /* Compact away revoked tags: they are not written to the descriptor at all. */
    int n = 0;
    for (int i = 0; i < j_ntags; i++) {
        if (jbd2_is_revoked(j_tag_block[i])) continue;
        j_tag_block[n] = j_tag_block[i];
        j_tag_slot[n]  = j_tag_slot[i];
        n++;
    }

    /* An empty transaction must NOT advance the sequence. Nothing was written to
     * the log, so the next transaction written will still carry j_sequence;
     * bumping it here would leave a gap, and replay -- which stops at the first
     * sequence that is not the expected successor -- would stop at that gap and
     * silently skip every real transaction after it. */
    if (n == 0) { jbd2_reset_txn(); return 0; }

    uint32_t revoke_bytes = j_revoke_hwm ? JBD2_REVOKE_SIZE(j_revoke_hwm) : 0;

    int per_desc = (int)(j_blocksize - sizeof(struct jbd2_header)) / j_tag_bytes;
    if (per_desc < 1) return -1;
    int ndesc = (n + per_desc - 1) / per_desc;
    if (ndesc < 1) ndesc = 1;

    /* The revoke table lives in the final descriptor block. If it does not fit
     * alongside that block's tags, give the block over to the table and push its
     * tags into an extra descriptor. */
    if (revoke_bytes) {
        int last_tags = n - (ndesc - 1) * per_desc;
        if (last_tags < 0) last_tags = 0;
        while (revoke_bytes + (uint32_t)sizeof(struct jbd2_header)
               + (uint32_t)last_tags * j_tag_bytes > j_blocksize) {
            ndesc++;
            last_tags = n - (ndesc - 1) * per_desc;
            if (last_tags < 0) last_tags = 0;
            if (ndesc > n) break;
        }
    }

    if (jbd2_txn_blocks(n) + (ndesc - 1) + (revoke_bytes ? 1 : 0)
        + JBD2_WRAP_MARGIN > (int)(j_maxlen - j_head)) {
        kprintf("jbd2: transaction will not fit in journal (%d tags)\n", n);
        return -1;
    }

    uint32_t at = j_head;
    uint32_t seq = j_seq_next;

    /* ── 1. descriptor block(s): the tag list ── */
    int i = 0;
    for (int d = 0; d < ndesc; d++) {
        int is_last = (d == ndesc - 1);
        int placed = 0;
        int off = (int)sizeof(struct jbd2_header);

        memset(j_descbuf, 0, j_blocksize);
        struct jbd2_header *h = (struct jbd2_header *)j_descbuf;
        h->h_magic     = le32(JBD2_MAGIC_NUMBER);
        h->h_blocktype = le32(JBD2_DESCRIPTOR_BLOCK);
        h->h_sequence  = le32(seq);

        while (i < n && placed < per_desc) {
            if (is_last && revoke_bytes &&
                off + (int)j_tag_bytes + (int)revoke_bytes > (int)j_blocksize)
                break;                       /* keep room for the revoke table */

            uint8_t *t = j_descbuf + off;
            *(uint32_t *)(t + 0) = le32(j_tag_block[i]);
            *(uint16_t *)(t + 4) = le16(crc16_ccitt(
                j_stage + (uint32_t)j_tag_slot[i] * j_blocksize,
                j_blocksize));
            *(uint16_t *)(t + 6) = le16((uint16_t)
                                     ((i == n - 1) ? JBD2_FLAG_LAST_TAG : 0));
            off += (j_tag_bytes + 3) & ~3;   /* tags start 4-byte aligned */
            placed++;
            i++;
        }

        /* A transaction must name at least one block, even if every tag was
         * revoked -- an empty descriptor is not something replay can read. */
        if (placed == 0) {
            uint8_t *t = j_descbuf + sizeof(struct jbd2_header);
            *(uint32_t *)(t + 0) = le32(0);
            *(uint16_t *)(t + 6) = le16(JBD2_FLAG_LAST_TAG);
            off += (j_tag_bytes + 3) & ~3;
        }

        if (is_last && revoke_bytes) {
            uint32_t words = revoke_bytes / 4;
            for (uint32_t w = 0; w < words; w++) {
                uint32_t word = 0;
                for (int b = 0; b < 32; b++) {
                    uint32_t blk = w * 32 + (uint32_t)b;
                    if (jbd2_is_revoked(blk)) word |= 1u << b;
                }
                *(uint32_t *)(j_descbuf + off + w * 4) = le32(word);
            }
        }

        if (jwrite(at++, j_descbuf) < 0) return -1;
    }

    /* ── 2. journal data blocks, in tag order ── */
    for (int k = 0; k < n; k++)
        if (jwrite(at++, j_stage + (uint32_t)j_tag_slot[k] * j_blocksize) < 0)
            return -1;

    /* ── 3. commit block: the point of no return ── */
    memset(j_descbuf, 0, j_blocksize);
    struct jbd2_commit *c = (struct jbd2_commit *)j_descbuf;
    c->h.h_magic     = le32(JBD2_MAGIC_NUMBER);
    c->h.h_blocktype = le32(JBD2_COMMIT_BLOCK);
    c->h.h_sequence  = le32(seq);
    c->h_chksum_type = JBD2_CRC32_CHKSUM;
    c->h_chksum_size = JBD2_CHECKSUM_BYTES;
    /* CRC32 over the commit header (h_magic through h_padding, 16 bytes). */
    c->h_chksum[0]   = le32(crc32(j_descbuf, 16));
    if (jwrite(at++, j_descbuf) < 0) return -1;

    /* ── 4. home locations, only now that the commit is durable ── */
    for (int k = 0; k < n; k++)
        ext2_write_block_from(j_tag_block[k],
                              j_stage + (uint32_t)j_tag_slot[k] * j_blocksize);

    j_head = at;
    j_sequence = seq + 1;
    jbd2_reset_txn();
    return 0;
}

/* ── the write path hook ────────────────────────────────────────────────
 * Called by ext2.c in place of a direct block write. Returns non-zero if the
 * caller must also write the block itself; with a journal present it must not.
 */
int jbd2_stage_block(uint32_t fs_block, const void *data) {
    if (!j_present) return 1;                     /* no journal: write directly */
    if (j_txn_depth == 0 && jbd2_txn_begin() < 0) return 1;

    if (j_ntags >= JBD2_MAX_TAGS) {
        /* The staging area is full, so this write cannot wait for the caller's
         * commit point. End the transaction here and start another.
         *
         * The depth is saved and restored around the flush so that an enclosing
         * caller's transaction stays open: this split is a memory-pressure
         * boundary, not the end of the caller's operation. Correctness is
         * unaffected either way, because every committed transaction is atomic
         * on its own -- a crash may leave a file shorter than intended, which is
         * the same guarantee ext3 gives.
         *
         * `data` is snapshot into j_save first, and that is load-bearing, not
         * tidiness. The only buffer the caller has is ext2.c's shared block_buf,
         * and jbd2_flush() reaches the disk through ext2.c -- whose own
         * read_inode_block() uses that same buffer as scratch. Without the
         * snapshot, the flush leaves its scratch behind and the memcpy below
         * stages the journal's indirect block, which then gets written to the
         * caller's target. With j_map the flush no longer needs the buffer for
         * journal addressing, but ext2_write_block_from is not the only thing
         * that can touch it, and this is the one place where the two modules
         * share state implicitly.
         */
        memcpy(j_save, data, j_blocksize);
        data = j_save;
        int depth = j_txn_depth;
        if (jbd2_flush() < 0) return 1;
        j_txn_depth = depth;
        jbd2_reset_txn();
        j_seq_next = j_sequence;
    }

    j_tag_block[j_ntags] = fs_block;
    j_tag_slot[j_ntags]  = (uint32_t)j_ntags;
    memcpy(j_stage + (uint32_t)j_ntags * j_blocksize, data, j_blocksize);
    j_ntags++;
    return 0;    /* staged; the journal will place it */
}

void jbd2_note_free(uint32_t fs_block) {
    jbd2_revoke(fs_block);
}

int jbd2_peek_block(uint32_t fs_block, void *buf) {
    if (!j_present) return -1;
    /* Backwards, so a block staged more than once in the transaction reads back
     * as its newest version. See the note in jbd2.h for why this is required at
     * all rather than merely being nice. */
    for (int i = j_ntags - 1; i >= 0; i--)
        if (j_tag_block[i] == fs_block) {
            memcpy(buf, j_stage + (uint32_t)j_tag_slot[i] * j_blocksize, j_blocksize);
            return 0;
        }
    return -1;
}

int jbd2_have_journal(void) { return j_present; }
int jbd2_in_transaction(void) { return j_present && j_txn_depth > 0; }

/* ── replay ──────────────────────────────────────────────────────────────
 * Walk the log from the recorded start, applying committed transactions in
 * order. Stops at the first block that is not a descriptor carrying the
 * expected sequence, at a missing commit block, or at the end of the journal.
 *
 * The sequence walk is what makes stopping safe: a leftover block from an older
 * epoch has an older sequence, so it ends the walk rather than being misapplied.
 * It also means replaying a journal that is already up to date is harmless --
 * every transaction rewrites the same bytes -- so we always replay rather than
 * maintaining the clean-shutdown checksum bookkeeping that ext3 uses to skip it.
 */
static int jbd2_replay_one(uint32_t *pos, uint32_t expect,
                           uint32_t *tagbuf, int *ntags) {
    uint32_t at = *pos;
    int n = 0, done = 0;
    uint16_t tag_cksum[JBD2_MAX_TAGS];  /* CRC16 per tag, for verification */

    while (!done) {
        if (at >= j_maxlen) return -1;
        struct jbd2_header h;
        if (jread_hdr(at, &h) < 0) return -1;
        if (be32(h.h_magic) != JBD2_MAGIC_NUMBER) return -1;
        if (be32(h.h_blocktype) != JBD2_DESCRIPTOR_BLOCK) return -1;
        if (be32(h.h_sequence) != expect) return -1;

        uint32_t dblk = at;
        at++;
        if (jread_block(dblk, j_iobuf) < 0) return -1;

        int off = (int)sizeof(struct jbd2_header);
        int cap = (int)(j_blocksize - sizeof(struct jbd2_header));
        while (off + (int)j_tag_bytes <= cap) {
            uint8_t *t = j_iobuf + off;
            uint16_t flags = be16(*(uint16_t *)(t + 6));
            if (flags & JBD2_FLAG_ESCAPE) { done = 1; break; }
            if (!(flags & JBD2_FLAG_DELETED) && n < JBD2_MAX_TAGS) {
                tagbuf[n] = be32(*(uint32_t *)(t + 0));
                tag_cksum[n] = be16(*(uint16_t *)(t + 4));
                n++;
            }
            if (flags & JBD2_FLAG_LAST_TAG) { done = 1; break; }
            off += (j_tag_bytes + 3) & ~3;
        }
    }

    /* Data blocks, one per tag. Verify CRC16 checksum of each block
     * against the tag's t_checksum. Staged content is applied only
     * after the commit block is confirmed, so a truncated transaction
     * writes nothing. */
    for (int i = 0; i < n; i++) {
        if (at >= j_maxlen) return -1;
        if (jread_block(at, j_iobuf) < 0) return -1;
        if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_CSUM_V2) {
            uint16_t computed = crc16_ccitt(j_iobuf, j_blocksize);
            if (computed != tag_cksum[i]) return -1;
        }
        memcpy(j_stage + (uint32_t)i * j_blocksize, j_iobuf, j_blocksize);
        at++;
    }

    /* No commit block means the transaction never committed: discard it whole. */
    if (at >= j_maxlen) return -1;
    if (jread_block(at, j_iobuf) < 0) return -1;
    if (be32(*(uint32_t *)(j_iobuf)) != JBD2_MAGIC_NUMBER) return -1;
    if (be32(*(uint32_t *)(j_iobuf + 4)) != JBD2_COMMIT_BLOCK) return -1;
    if (be32(*(uint32_t *)(j_iobuf + 8)) != expect) return -1;
    at++;

    /* Verify commit-block CRC32 over the first 16 bytes
     * (h_magic through h_padding). */
    if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_CSUM_V2) {
        uint32_t computed = crc32(j_iobuf, 16);
        uint32_t stored = be32(*(uint32_t *)(j_iobuf + 16));
        if (computed != stored) return -1;
    }

    for (int i = 0; i < n; i++)
        ext2_write_block_from(tagbuf[i], j_stage + (uint32_t)i * j_blocksize);

    *ntags = n;
    *pos = at;
    return 0;
}

static int jbd2_replay(void) {
    uint32_t pos = j_first;
    uint32_t seq = j_sequence;
    uint32_t tagbuf[JBD2_MAX_TAGS];
    int applied = 0;

    while (pos < j_maxlen) {
        int n = 0;
        if (jbd2_replay_one(&pos, seq, tagbuf, &n) < 0) break;
        applied++;
        seq++;
    }
    j_head = pos;
    j_sequence = seq;
    return applied;
}

/* ── mount / unmount ───────────────────────────────────────────────────── */

/* Release everything jbd2_init() may have allocated. Tolerates a partially
 * built state, because the point is to be callable from the failure paths. */
static void jbd2_free_buffers(void) {
    if (j_sb)         { free(j_sb);         j_sb = 0; }
    if (j_probe)      { free(j_probe);      j_probe = 0; }
    if (j_stage)      { free(j_stage);      j_stage = 0; }
    if (j_iobuf)      { free(j_iobuf);      j_iobuf = 0; }
    if (j_descbuf)    { free(j_descbuf);    j_descbuf = 0; }
    if (j_revoke_bits){ free(j_revoke_bits);j_revoke_bits = 0; }
    if (j_save)       { free(j_save);       j_save = 0; }
    if (j_map)        { free(j_map);        j_map = 0; }
    j_map_len = 0;
}

int jbd2_init(uint32_t journal_inode) {
    if (j_present) return 0;
    if (!journal_inode) {
        kprintf("ext3: HAS_JOURNAL is set but s_journal_inum is 0\n");
        return -1;
    }

    j_inum = journal_inode;
    j_sb = (uint8_t *)malloc(JBD2_HEADER_SIZE);
    if (!j_sb) return -1;

    /* j_probe has to exist before the superblock is read, because the block size
     * is not known until then and every read has to land somewhere full-size. */
    j_probe = (uint8_t *)malloc(JBD2_MAX_BLOCK_SIZE);
    if (!j_probe) { free(j_sb); j_sb = 0; return -1; }

    if (jbd2_load_sb() < 0) {
        kprintf("ext3: inode %u has no readable journal superblock\n", j_inum);
        jbd2_free_buffers();
        return -1;
    }

    /* csum_v2 IS implemented (10-byte tags with CRC16 data-block
     * checksums), so we accept it. Anything else falls back to
     * journalless ext2. */
    if (j_tag_bytes != JBD2_TAG_SIZE_DEFAULT && j_tag_bytes != 10) {
        const char *why = "?";
        if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_CSUM_V3) why = "csum_v3";
        else if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_64BIT) why = "64bit";
        else if (j_feature_incompat & JBD2_FEATURE_INCOMPAT_CSUM_V2) why = "csum_v2";
        kprintf("ext3: journal needs the %s tag layout (%d B), not implemented"
                " -- mounting without a journal\n", why, j_tag_bytes);
        jbd2_free_buffers();
        return -1;
    }

    /* Enable csum_v2 in memory: we compute CRC16 for every data-block tag
     * and CRC32 for every commit block. The disk superblock is NOT
     * modified (writing it can corrupt e2fsck verification); we rely
     * on our own knowledge that our tags carry checksums. */
    j_feature_incompat |= JBD2_FEATURE_INCOMPAT_CSUM_V2;
    j_tag_bytes = 10;

    /* One bit per filesystem block, not per journal block. */
    uint32_t fs_blocks = ext2_block_count();
    j_revoke_bytes = (fs_blocks + 7) / 8;
    if (j_revoke_bytes == 0) j_revoke_bytes = 1;

    /* The journal block size must match the filesystem's, and the reason is a
     * buffer rather than a format preference: the journal stages whole blocks
     * through ext2.c's block buffer, which is sized for the filesystem's block
     * size, so a larger journal block would read and write past it. mke2fs never
     * creates such a journal (s_blocksize is a copy of the fs block size), so
     * this is a refusal of malformed input rather than a real layout. */
    if (j_blocksize != ext2_block_size()) {
        kprintf("ext3: journal block size %u does not match filesystem block"
                " size %u -- mounting without a journal\n",
                j_blocksize, ext2_block_size());
        jbd2_free_buffers();
        return -1;
    }

    /* Now that j_maxlen is known, resolve the journal's block map. This is an
     * optimisation with a correctness motive: without it, every journal block
     * access walks the journal inode, and that walk happens in the filesystem
     * driver's shared block buffer -- so a flush running between "caller filled
     * block_buf" and "caller staged block_buf" silently staged the journal
     * inode's indirect block instead. Failure here only costs speed; jphys()
     * falls back to walking the inode, which is correct. */
    if (jbd2_build_map() < 0)
        kprintf("ext3: journal block map not cached (%u blocks), using slow path\n",
                j_maxlen);

    j_stage = (uint8_t *)malloc(JBD2_STAGE_BLOCKS * j_blocksize);
    j_iobuf = (uint8_t *)malloc(j_blocksize);
    j_descbuf = (uint8_t *)malloc(j_blocksize);
    j_revoke_bits = (uint8_t *)malloc(j_revoke_bytes);
    j_save = (uint8_t *)malloc(j_blocksize);
    if (!j_stage || !j_iobuf || !j_descbuf || !j_revoke_bits || !j_save) {
        kprintf("ext3: cannot allocate journal buffers (stage %u, io %u,"
                " desc %u, revoke %u, save %u)\n",
                JBD2_STAGE_BLOCKS * j_blocksize, j_blocksize, j_blocksize,
                j_revoke_bytes, j_blocksize);
        jbd2_free_buffers();
        return -1;
    }
    memset(j_revoke_bits, 0, j_revoke_bytes);

    j_present = 1;
    jbd2_reset_txn();

    int n = jbd2_replay();
    kprintf("ext3: journal on inode %u, %u x %u B, %d B tags, log at block %u\n",
            j_inum, j_maxlen, j_blocksize, j_tag_bytes, j_head);
    if (n > 0)
        kprintf("ext3: replayed %d committed transaction%s\n", n, n == 1 ? "" : "s");
    else
        kprintf("ext3: journal clean, nothing to replay\n");
    return 0;
}

void jbd2_shutdown(int clean) {
    if (!j_present) return;
    /* `clean` does not change what the journal does: the log position is
     * recorded either way, because the journal has to be left in a state the
     * next mount can walk. Whether the FILESYSTEM claims a clean unmount is the
     * superblock's business (s_state), and ext2_unmount() sets that separately.
     * Honouring it here too would mean marking the fs clean while leaving
     * replayable transactions on disk, which is exactly the state that makes
     * e2fsck report a filesystem needing recovery. */
    (void)clean;
    if (j_txn_depth > 0) { j_txn_depth = 1; jbd2_txn_commit(); }
    jbd2_store_sb(j_head, j_sequence);
    j_present = 0;
    /* Free the staging and map buffers here rather than leaking them: unmount is
     * not necessarily terminal (a remount re-runs jbd2_init, and jbd2_build_map
     * would otherwise hand back a second copy of the journal's block map). */
    jbd2_free_buffers();
}
