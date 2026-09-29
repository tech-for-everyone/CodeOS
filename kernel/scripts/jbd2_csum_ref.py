#!/usr/bin/env python3
"""Host-side reference for JBD2's CRC32C, and an oracle for the kernel port.

Why this file exists
--------------------
kernel/kernel/jbd2.c now writes and verifies four different JBD2 checksums
under csum_v2/csum_v3.  Four checksums that are merely plausible is exactly the
failure mode worth designing against: a journal whose checksums are wrong does
not fail loudly, it is simply *rejected* by every real reader, and e2fsck's
response to a journal it cannot trust is to recover nothing and exit 0.

So the convention is pinned here against something other than itself:

  1. crc32c_kat() reproduces e2fsprogs' own unit test -- the `test_buf` and the
     128-entry (seed, offset, length, expected) table from
     lib/ext2fs/crc32c.c:test_crc32c, embedded verbatim below.  The algorithm
     this validates is the plain Sarwate loop: reflected, polynomial
     0x82f63b78, and the caller's seed used verbatim as the running CRC, with
     no pre-inversion and no final xor.  That last part is the trap -- it is
     the opposite convention from the CRC32 the rest of the kernel uses, and
     jbd2_superblock_csum() genuinely does start from ~0.

  2. `roundtrip` builds a real csum_v2 journal on an ext3 image, poisons a
     home block, and asks e2fsck to replay it -- once with every checksum
     correct, and once per checksum deliberately corrupted.  e2fsprogs is an
     independent implementation, so agreement there is evidence in a way that
     agreement with our own code never is.  The corrupted runs matter most:
     they prove the check is live, and that a wrong checksum is *detected*
     rather than ignored.

     Verifying all four this way found the thing the documentation does not
     mention: JBD2_FLAG_LAST_TAG is not decorative.  count_tags() walks the tag
     stream until it sees it, so a descriptor without it is read as an unbounded
     run of tags made of zero padding, and the transaction replays nothing at
     all while still exiting 0 -- the same silent-success failure as a bad
     checksum, arrived at by a different route.

Usage
-----
    python3 jbd2_csum_ref.py kat       # check the CRC32C against e2fsprogs
    python3 jbd2_csum_ref.py roundtrip # the four-checksum replay experiment
    python3 jbd2_csum_ref.py all
"""

import base64
import os
import re
import struct
import subprocess
import sys

# ── CRC32C ───────────────────────────────────────────────────────────────────
# Reflected, polynomial 0x82f63b78.  The caller's value is the running CRC
# verbatim: no pre-inversion, no final xor.  See the module docstring.
_CRC32C_POLY = 0x82F63B78


def _crc32c_table():
    tab = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ (_CRC32C_POLY if c & 1 else 0)
        tab.append(c)
    return tab


_CRC32C_TAB = _crc32c_table()


def crc32c(crc, data):
    for b in data:
        crc = (crc >> 8) ^ _CRC32C_TAB[(crc ^ b) & 0xFF]
    return crc & 0xFFFFFFFF


# ── e2fsprogs' own test vectors, embedded ────────────────────────────────────
# lib/ext2fs/crc32c.c: test_buf (4096 bytes) and
# static struct crc_test test[] = { seed, offset, length, crc32c_le, crc32c_be }.
# The little-endian column is the one the test compares against; the table below
# keeps the e2fsprogs seed/offset/length triples unchanged.
_KAT_BUF_B64 = (
    "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"
)


_KAT_VECTORS = [
    (0xFFFFFFFF, 0, 4096, 0x13934BEF),
    (0xFE7328EA, 1891, 1815, 0xED2C0D70),
    (0x4C40684E, 1825, 286, 0xD7F46CCC),
    (0x6B487F90, 612, 1980, 0x759E9939),
    (0x9F5810DB, 2810, 597, 0x2685197F),
    (0xB15C4755, 3419, 676, 0xD8FADCB5),
    (0x06518253, 4091, 4, 0xABEE2433),
    (0xD9E71C55, 2602, 601, 0x96682AF2),
    (0x0C1AE843, 3300, 795, 0x7B637C43),
    (0xEC3CD517, 767, 1382, 0x5D719A77),
    (0x77828E95, 1663, 911, 0x43EE5B6C),
    (0xEC87B4E3, 3356, 739, 0x2DDD2EEE),
    (0x412158BB, 3822, 273, 0x67B38BA2),
    (0x2E52DE3E, 3146, 949, 0xBCC5D61D),
    (0x6DDAAE8B, 3481, 614, 0x8B535544),
    (0x049B6CB1, 2501, 176, 0xFC22CABC),
    (0x77D4B954, 650, 2042, 0x71D00923),
    (0x5E192355, 2753, 506, 0xB966B81A),
    (0x7D80B71D, 531, 480, 0x2BBA371A),
    (0x01F6F1E4, 470, 917, 0xB7E8A647),
    (0x1DFABB13, 3604, 491, 0x53917FBA),
    (0xB00A4449, 3062, 1033, 0xEDECB577),
    (0x7ECD3981, 2111, 363, 0xEFEF62B9),
    (0xF8F330D2, 1214, 1879, 0x9357C9F3),
    (0x03C38AF2, 3363, 732, 0x360FA8C0),
    (0x687BB79B, 3901, 194, 0x448D3BE2),
    (0x6710F550, 2537, 1539, 0xDBFD1998),
    (0x873171D1, 1927, 1237, 0xAB7F1B62),
    (0x373B1314, 3855, 240, 0x184098AB),
    (0x90FAD9CD, 3757, 338, 0x23CE52FF),
    (0x19676FE7, 125, 1805, 0xF8A76F1E),
    (0x89FACD45, 1523, 1139, 0x4331A006),
    (0x6F173747, 4035, 60, 0xB012F08E),
    (0x4B44A106, 1882, 139, 0xF6F7AC38),
    (0xB620AD06, 1908, 382, 0xD34558E6),
    (0x976F21E9, 2263, 842, 0xE533AA3A),
    (0x687628C0, 1733, 1563, 0x3A840B15),
    (0xE24AC108, 3280, 815, 0x51010AE8),
    (0x361C44A3, 772, 1817, 0xFD7BD481),
    (0xD93FF95E, 3511, 142, 0xCFBBC304),
    (0xED752D12, 2179, 145, 0x65A6C868),
    (0xB4FF4B54, 979, 449, 0xF82597E7),
    (0x111B520F, 1800, 235, 0xC3E109F3),
    (0x62C806F2, 2979, 1116, 0x874D3A72),
    (0x40D97470, 1505, 1421, 0x87A9684F),
    (0x4312179C, 86, 1806, 0x809A00F5),
    (0x13D5F84C, 2605, 260, 0xF3D27578),
    (0x1F302CB2, 337, 20, 0x1E162693),
    (0xE491DB24, 1536, 1782, 0x7FF09615),
    (0xF9A98069, 698, 685, 0x01AF7387),
    (0xE9C477AD, 351, 1912, 0x6FACF9A0),
    (0x353F32B2, 2172, 1923, 0x6CC964EA),
    (0x78E1B24F, 1616, 1704, 0xB3BB7C27),
    (0x61AA400E, 73, 596, 0xB8CD1681),
    (0xB84B10B0, 3955, 140, 0x406A6450),
    (0x9FA99C9C, 2684, 1239, 0xFB3D21B4),
    (0x3FC9EBE3, 3289, 214, 0x43803F9C),
    (0x529879CD, 754, 1429, 0x78B4C6A6),
    (0x3A933019, 1302, 614, 0xDCB45436),
    (0x887B4977, 551, 909, 0xC5F7C3D9),
    (0x770745DE, 2246, 1849, 0xF69145E8),
    (0x28BE3B47, 3142, 811, 0x764C028F),
    (0x5013A050, 3318, 777, 0xEA8FE164),
    (0x2EC4C9BA, 1768, 1933, 0xA35557A9),
    (0xA9F950C9, 3379, 716, 0x41EA8618),
    (0x5B520229, 1970, 1156, 0x44569F1F),
    (0xD8DCBBFC, 47, 1164, 0xDB88AB8B),
    (0x25529792, 3357, 738, 0x20CDA404),
    (0x9F3F6D71, 568, 1946, 0x0720443E),
    (0x64121215, 2047, 911, 0x6AACFF2C),
    (0xFB6CDDE0, 3832, 263, 0xBD43A0F1),
    (0x221C9D6F, 1974, 335, 0xB67F834B),
    (0x030E1DE4, 2102, 1204, 0x0D67D26A),
    (0xB56FA6CF, 3079, 1016, 0x60601AC1),
    (0xB55C89F5, 2446, 468, 0x2400EFBE),
    (0x5E90B6D5, 1803, 1002, 0x3BB5D6EA),
    (0x2A7045AE, 2401, 1587, 0xFCA89E4B),
    (0x8B374EA9, 1722, 1920, 0xBCE036ED),
    (0x8BD90BC9, 1378, 873, 0xCB26A24B),
    (0x5B1B1762, 253, 1306, 0x33CDDA07),
    (0xA4153555, 1423, 1479, 0xBE50EECA),
    (0x0BE1F931, 1617, 1650, 0x95A25753),
    (0xB7E78618, 2687, 699, 0xE06BCC1C),
    (0x4A9BC41B, 3665, 430, 0x709E8D2C),
    (0xFC359D13, 1088, 760, 0x0A58451F),
    (0x5AA48619, 1745, 644, 0x928EAD83),
    (0xA609AFA8, 1342, 626, 0xB048C141),
    (0x3F108AFB, 2377, 336, 0x9A6BB5BC),
    (0x79BEC2D3, 2285, 1810, 0x32692D57),
    (0x9429E067, 3011, 1084, 0x5295CEFF),
    (0xAE58B96A, 2093, 2002, 0xC2A681BA),
    (0x95DF24BE, 2437, 1217, 0x3A287765),
    (0x5E94976F, 1430, 1261, 0xFF00C489),
    (0xF5E5F1DE, 3377, 718, 0x35F28E91),
    (0xA2C219CF, 2620, 884, 0x707D21EB),
    (0xF21B6CEB, 2329, 309, 0x0847FB8B),
    (0xAA988728, 1927, 1905, 0x885AEAA4),
    (0xAA5DFAAC, 997, 1307, 0x52C48AB7),
    (0x0A053968, 3370, 725, 0x7A90256D),
    (0x1421DC20, 3823, 272, 0x97D6DA24),
    (0xB47C2166, 2666, 521, 0xCFD6CC52),
    (0x77DD1955, 222, 614, 0xBA74BCAA),
    (0x68A03CC2, 2095, 1968, 0x752BD5D8),
    (0x0226B0A3, 2655, 1440, 0x82DE4970),
    (0x637BF3B1, 3475, 620, 0x5C7115CB),
    (0x3B120EDF, 3091, 1004, 0x80D7D20F),
    (0xE2456780, 747, 1601, 0xC0A5D289),
    (0x9B2E7125, 3084, 1011, 0xCC15F57E),
    (0x153033EF, 1927, 1718, 0x3CDE443B),
    (0x18458B3F, 1644, 1377, 0x9A2BD8C6),
    (0x4FF9D4B9, 3215, 826, 0xD0EE6D6D),
    (0xDF84B5D9, 2050, 666, 0xDAB0D74A),
    (0x81EE15DF, 974, 1829, 0x9942E2DE),
    (0x5C768E04, 2813, 352, 0x36110831),
    (0xE5E18094, 2891, 160, 0xFFA3E4A7),
    (0xED7263B6, 3341, 754, 0xB0006A35),
    (0x5BFDE7D7, 1787, 1364, 0xA4193B76),
    (0x67F4A743, 2949, 1146, 0xF05C8D8F),
    (0xF13BDF22, 4087, 8, 0x816351EB),
    (0x08ECC608, 3421, 152, 0x90492772),
    (0x296F52BA, 1273, 1928, 0x5E5A4896),
    (0xBE4624C2, 1063, 1263, 0xCD267B94),
    (0x906F7C7C, 2565, 63, 0x03FCFC33),
    (0x8F7B323E, 1112, 1223, 0xCD4969C8),
    (0x88D6593D, 1431, 1461, 0xF199CD3B),
    (0x978A7768, 616, 467, 0xB28C95BD),
    (0x857A621E, 1959, 936, 0xF4BF84AB),
    (0xB0E121EF, 1470, 1604, 0x28747C14),
]


def _kat_buf():
    return base64.b64decode(_KAT_BUF_B64)


def crc32c_kat(verbose=True):
    """Reproduce e2fsprogs' test_crc32c vectors. Returns (passed, total)."""
    buf = _kat_buf()
    bad = []
    for i, (seed, off, length, want_le) in enumerate(_KAT_VECTORS):
        got = crc32c(seed, buf[off:off + length])
        if got != want_le:
            bad.append((i, got, want_le))
    if verbose:
        if bad:
            for i, got, want in bad[:5]:
                print(f"  vector {i}: got 0x{got:08x} want 0x{want:08x}")
        print(f"crc32c: {len(_KAT_VECTORS) - len(bad)}/{len(_KAT_VECTORS)}"
              f" vectors match e2fsprogs")
    return len(_KAT_VECTORS) - len(bad), len(_KAT_VECTORS)


# JBD2 field offsets and feature bits, from include/linux/jbd2.h
JBD2_MAGIC              = 0xC03B3998
JBD2_DESCRIPTOR         = 1
JBD2_COMMIT             = 2
JBD2_INCOMPAT_REVOKE    = 0x01
JBD2_INCOMPAT_64BIT     = 0x02
JBD2_INCOMPAT_ASYNC     = 0x04
JBD2_INCOMPAT_CSUM_V2   = 0x08
JBD2_INCOMPAT_CSUM_V3   = 0x10
JBD2_INCOMPAT_FAST_COM  = 0x20
JBD2_CRC32C_CHKSUM      = 4

SB_SEQ, SB_START = 0x18, 0x1C
SB_COMPAT, SB_INCOMPAT, SB_UUID = 0x24, 0x28, 0x30
SB_CSUM_TYPE, SB_CHECKSUM, SB_SIZE = 0x50, 0xFC, 0x400

FLAG_ESCAPE, FLAG_SAME_UUID, FLAG_DELETED, FLAG_LAST_TAG = 1, 2, 4, 8

BS = 1024


# ── the replay experiment ────────────────────────────────────────────────────

def be32(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def _sh(*a):
    return subprocess.run(a, capture_output=True, text=True)


def _dbg(img, *cmd):
    return _sh("/usr/sbin/debugfs", *sum([["-R", c] for c in cmd], []), img).stdout


def _build(img, stage):
    """mke2fs an ext3 image with one real file that owns a data block."""
    for p in (img,):
        if os.path.exists(p):
            os.unlink(p)
    with open(img, "wb") as f:
        f.truncate(64 * 1024 * 1024)
    os.makedirs(os.path.join(stage, "etc"), exist_ok=True)
    with open(os.path.join(stage, "etc", "conf.txt"), "wb") as f:
        f.write(b"original v1 content\n" * 50)
    r = _sh("/usr/sbin/mke2fs", "-q", "-t", "ext3", "-b", str(BS),
            "-I", "128", "-F", "-d", stage, img)
    if r.returncode != 0:
        raise SystemExit(f"mke2fs: {r.stdout}{r.stderr}")


def _journal_first_block(img):
    """The journal inode's first *data* block -- where the superblock lives.

    `imap` reports where the inode is stored, not where the file's data starts;
    reading the journal superblock from there patches an unrelated block, and
    the result looks like filesystem corruption rather than a parsing mistake.
    """
    for line in _dbg(img, "bmap <8> 0").splitlines():
        if line.strip().isdigit():
            return int(line.strip())
    raise SystemExit("bmap <8> 0 gave nothing")


def _target_block(img, path="/etc/conf.txt"):
    """The filesystem block holding byte 0 of `path`.

    An ext3 file prints its map as classic block pointers under "BLOCKS:", one
    per line, as "(0):4644" -- not the ext4 "EXTENTS:" form.
    """
    in_blocks = False
    for line in _dbg(img, f"stat {path}").splitlines():
        if line.strip() == "BLOCKS:":
            in_blocks = True
            continue
        if in_blocks:
            s = line.strip()
            if not s or s.startswith("TOTAL"):
                continue
            m = re.fullmatch(r"\((\d+)\):(\d+)", s)
            if m:
                return int(m.group(2))
            raise SystemExit(f"unparsed block pointer {s!r}")
    raise SystemExit(f"no block map for {path}")


def _rd(img, blk, n=BS):
    with open(img, "rb") as f:
        f.seek(blk * BS)
        return bytearray(f.read(n))


def _wr(img, blk, data):
    with open(img, "r+b") as f:
        f.seek(blk * BS)
        f.write(bytes(data))


def _enable_csum_v2(sb, start, seq):
    """Make the journal superblock a conformant csum_v2 one.

    Three things have to hold together, and the kernel checks all three:
    the CSUM_V2 feature bit, s_checksum_type == JBD2_CRC32C_CHKSUM (a u8 at
    0x50 -- read as a be32 it picks up its own padding), and an s_checksum over
    the whole 1024-byte superblock.  Any one wrong and the mount is refused.
    """
    incompat = struct.unpack_from(">I", sb, SB_INCOMPAT)[0]
    struct.pack_into(">I", sb, SB_INCOMPAT, incompat | JBD2_INCOMPAT_CSUM_V2)
    struct.pack_into(">I", sb, SB_SEQ, seq)
    struct.pack_into(">I", sb, SB_START, start)
    sb[SB_CSUM_TYPE] = JBD2_CRC32C_CHKSUM
    struct.pack_into(">I", sb, SB_CHECKSUM, 0)
    struct.pack_into(">I", sb, SB_CHECKSUM, crc32c(0xFFFFFFFF, bytes(sb)))


def _transaction(seed, seq, home, data, uuid, break_=None):
    """Build descriptor + data + commit for a one-tag csum_v2 transaction.

    The four checksums, each of which kernel/kernel/jbd2.c has to produce:

        superblock  crc32c(~0, sb, 1024), s_checksum read as zero
        tag         crc32c(crc32c(seed, be32(seq), 4), data) -- low 16 bits
        tail        crc32c(seed, whole descriptor block, tail zeroed)
        commit      crc32c(seed, whole commit block, h_chksum[0] zeroed)
    """
    desc = bytearray(BS)
    struct.pack_into(">I", desc, 0, JBD2_MAGIC)
    struct.pack_into(">I", desc, 4, JBD2_DESCRIPTOR)
    struct.pack_into(">I", desc, 8, seq)

    csum32 = crc32c(crc32c(seed, be32(seq)), data)
    if break_ == "tag":
        csum32 ^= 0xFFFF
    # A 10-byte tag: csum_v2 without 64bit, so journal_tag_bytes() == 10.
    struct.pack_into(">I", desc, 12, home)                     # t_blocknr
    struct.pack_into(">H", desc, 16, csum32 & 0xFFFF)          # t_checksum
    # LAST_TAG is not decoration.  count_tags() walks until it sees this, so a
    # descriptor without it is read as an unbounded run of tags made of zero
    # padding -- the transaction then replays nothing and still exits 0.
    struct.pack_into(">H", desc, 18, FLAG_LAST_TAG)           # t_flags
    desc[22:38] = uuid                                         # 16-byte uuid

    tail = crc32c(seed, bytes(desc))
    if break_ == "tail":
        tail ^= 0xFFFFFFFF
    struct.pack_into(">I", desc, BS - 4, tail)   # jbd2_journal_block_tail is 4

    com = bytearray(BS)
    struct.pack_into(">I", com, 0, JBD2_MAGIC)
    struct.pack_into(">I", com, 4, JBD2_COMMIT)
    struct.pack_into(">I", com, 8, seq)
    # h_chksum_type and h_chksum_size stay 0 even under csum_v2:
    # jbd2_commit_block_csum_set() zeroes them explicitly.  h_chksum[0] is at
    # offset 16 -- three be32 header fields, two u8 descriptors, two pad bytes.
    struct.pack_into(">I", com, 16, 0)
    ccsum = crc32c(seed, bytes(com))
    if break_ == "commit":
        ccsum ^= 0xFFFFFFFF
    struct.pack_into(">I", com, 16, ccsum)
    return desc, bytearray(data), com


_PAYLOAD = (b"CHECKSUM-V2-PAYLOAD-" + bytes(range(96)) + b"\n" * 900 + b"." * BS)[:BS]


def _one_run(work, break_=None):
    """One build -> poison -> e2fsck cycle. Returns (restored, e2fsck rc, note)."""
    img = os.path.join(work, f"csum-{break_ or 'ok'}.img")
    stage = os.path.join(work, "stage")
    _build(img, stage)

    jfirst = _journal_first_block(img)
    home = _target_block(img)
    assert _PAYLOAD != bytes(_rd(img, home)), "payload must differ from disk"

    sb = _rd(img, jfirst, SB_SIZE)
    if struct.unpack_from(">I", sb, 0)[0] != JBD2_MAGIC:
        raise SystemExit(f"no journal magic at block {jfirst}")
    uuid = bytes(sb[SB_UUID:SB_UUID + 16])
    seed = crc32c(0xFFFFFFFF, uuid)

    # One transaction at log block 1 (s_first).  s_sequence 1 matches what
    # mke2fs left, so no sequence arithmetic is needed to keep this readable.
    desc, dat, com = _transaction(seed, 1, home, _PAYLOAD, uuid, break_)
    _wr(img, jfirst + 1, desc)
    _wr(img, jfirst + 2, dat)
    _wr(img, jfirst + 3, com)
    _enable_csum_v2(sb, 1, 1)
    _wr(img, jfirst, sb)

    # Poison the home copy, so a restore can only have come from the journal.
    _wr(img, home, b"POISONED-POISONED-POISONED" + b"." * (BS - 23))

    r = _sh("/usr/sbin/e2fsck", "-fy", img)
    out = (r.stdout + r.stderr).strip()
    note = next((l.strip() for l in out.splitlines()
                 if "journal" in l.lower() and
                 ("checksum" in l.lower() or "corrupt" in l.lower())), "")

    after = bytes(_rd(img, home))
    ok = after == _PAYLOAD
    rc2 = _sh("/usr/sbin/e2fsck", "-fn", img).returncode
    os.unlink(img)
    return ok, r.returncode, note, rc2


def roundtrip():
    """Prove each of the four checksums, and prove the probe can fail.

    The corrupted runs are the point.  A test where every variant "passes" is
    indistinguishable from a test that checks nothing, and the failure mode
    here is exactly that: e2fsck's answer to a journal it distrusts is to
    recover nothing and exit 0.
    """
    work = "/tmp/opencode"
    os.makedirs(work, exist_ok=True)
    allok = True
    print(f"{'corruption':<12} {'restored':<9} {'rc':<4} {'e2fsck follow-up':<19} note")
    for break_ in (None, "tag", "tail", "commit"):
        ok, rc, note, rc2 = _one_run(work, break_)
        want = break_ is None
        passed = (ok == want) and rc2 == 0
        allok &= passed
        label = break_ or "none"
        print(f"{label:<12} {str(ok):<9} {rc:<4} {str(rc2):<19} {note}")
    print("all four checksums confirmed" if allok else
          "FAILED: some checksum was not accepted, or was accepted when broken")
    return allok


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    ok = True
    if what in ("kat", "all"):
        passed, total = crc32c_kat()
        ok &= passed == total
    if what in ("roundtrip", "all"):
        ok &= roundtrip()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
