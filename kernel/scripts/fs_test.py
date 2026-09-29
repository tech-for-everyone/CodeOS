#!/usr/bin/env python3
"""Filesystem test harness for ext2/ext3/codefs.

Why this exists
---------------
The filesystem is the one subsystem where "the kernel says it worked" is worth
very little: a driver can happily return success while leaving the on-disk
image inconsistent, and the damage only shows up later. So the authority here is
the *host*, not the guest. After the guest has run its script we dd the
partition back out of the disk image and let real e2fsprogs judge the result:

    e2fsck -fn   -- structural check, never writes
    debugfs -R   -- inspect the actual tree the kernel produced

That needs no root: the partition is carved out with dd, so e2fsprogs sees the
filesystem at offset 0, which is the only offset it can read. (Neither
dumpe2fs nor e2fsck can read a filesystem embedded at a nonzero offset, which
is exactly why the Makefile's `-E offset=1048576` images are awkward to verify
by hand.)

Usage
-----
    ./fs_test.py --fstype ext3          # build image, boot, exercise, verify
    ./fs_test.py --fstype ext2          # same, for comparison
    ./fs_test.py --fstype ext3 --keep   # leave the image in place afterwards
    ./fs_test.py --fstype ext3 --journal-csum v2
        # same, on a journal with CRC32C checksums (csum_v2 / csum_v3 /
        # csum_v2+64bit).  mke2fs on this host cannot build one, so the
        # journal superblock is rewritten here and e2fsprogs is left to judge
        # the result; jbd2_csum_ref.py is the standalone oracle for the
        # checksums themselves.
"""

import argparse
import os
import pty
import re
import select
import shutil
import struct
import subprocess
import sys
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
KERNEL_DIR = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(KERNEL_DIR, ".."))
ISO = os.path.join(KERNEL_DIR, "codeos-1.0.iso")
KERNEL_BIN = os.path.join(KERNEL_DIR, "codeos-1-kernel.bin")
WORK = "/tmp/codeos-fs-test"

# Matches the Makefile's disk layout: msdos label, single 0x83 partition
# starting at sector 2048, so the filesystem sits at byte offset 1 MiB.
PART_START_SECTOR = 2048
PART_OFFSET = PART_START_SECTOR * 512
DISK_SIZE_M = 64

# The machine type, boot order and disk placement are all dictated by the
# driver's hardware, not by convenience.  Each of these was found by probing
# (see the notes in scripts/fs_test.py's git history); the short version:
#
#  * ata.c probes the legacy PATA ports (0x1F0/0x3F6), master only.  q35 has no
#    PATA controller at all -- only AHCI -- so on qemu -machine q35 an IDE disk
#    is simply invisible ("block: no disk detected").  i440fx, -machine pc, has
#    PATA, so that is what we must use.
#  * The disk therefore has to own the primary master slot.  A CD-ROM does not
#    contend for it: -cdrom lands on the secondary channel, so booting from the
#    ISO and the disk as if=ide,index=0 works at the same time.
#  * -boot order=d is mandatory once a disk is attached.  Without it SeaBIOS
#    prefers the hard disk, iPXE tries to netboot it, and the guest hangs before
#    it ever reaches storage init -- which looks exactly like "disk not
#    detected" and cost a debugging round.
#  * Booting the kernel directly with -kernel does NOT work: the binary has no
#    PVH ELF note, so qemu refuses it ("Error loading uncompressed kernel").
QEMU = ["qemu-system-x86_64", "-machine", "pc", "-m", "1G", "-smp", "2",
        "-vga", "none", "-nographic", "-monitor", "none",
        "-boot", "order=d", "-cdrom", ISO]

PROMPT = b"root# "
BOOT_TIMEOUT = 60
STEP_TIMEOUT = 30
BOOT_RETRIES = 3


def log(msg):
    print(msg, flush=True)


# ───────────────────────────── image construction ─────────────────────────────

def build_image(disk, fstype, populate=True, journal_csum=None, journal_kb=None):
    """Create a partitioned disk with a freshly-mkfs'd filesystem inside it.

    journal_csum, when set, rewrites the journal superblock afterwards to
    advertise a checksummed journal (see enable_journal_csum).  mke2fs on this
    host cannot do it itself: its /etc/mke2fs.conf has no [journal] section, and
    this build ignores MKE2FS_CONFIG, so `mke2fs -t ext3` can only ever produce
    a v1 journal with s_feature_incompat == 0.

    journal_kb, when set, passes -J size=<journal_kb/1024> to mke2fs so the
    journal is that many kilobytes.  A small journal exercises the JBD2
    ring-wrap path (the journal tail reaches the end of the journal buffer
    and wraps back) much sooner than the default 4MB journal, where
    jmaxlen=4096 blocks.  The value must be a multiple of 1024 and at
    least 1024 (mke2fs floor: 1024 filesystem blocks = 1MB with -b 1024).
    """
    if journal_kb is not None:
        if journal_kb < 1024 or journal_kb % 1024 != 0:
            log(f"journal_kb must be a multiple of 1024 and >= 1024, got {journal_kb}")
            return False
    if os.path.exists(disk):
        os.remove(disk)
    subprocess.run(["truncate", "-s", f"{DISK_SIZE_M}M", disk], check=True)
    subprocess.run(["parted", "-s", disk, "mklabel", "msdos"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["parted", "-s", disk, "mkpart", "primary", "ext2",
                    f"{PART_START_SECTOR}s", "100%"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    # -d populates from a directory, so no root and no loop mount is needed.
    stage = os.path.join(WORK, "stage")
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(os.path.join(stage, "etc"), exist_ok=True)
    os.makedirs(os.path.join(stage, "bin"), exist_ok=True)
    if populate:
        with open(os.path.join(stage, "etc", "conf.txt"), "w") as f:
            f.write("alpha content one\n")
        with open(os.path.join(stage, "bin", "tool.sh"), "w") as f:
            f.write("beta content two\n")

    cmd = ["mke2fs", "-q", "-t", fstype, "-F", "-L", "codeosfs",
           "-b", "1024", "-I", "128", "-E", f"offset={PART_OFFSET}"]
    if journal_kb is not None:
        cmd += ["-J", f"size={journal_kb // 1024}"]
    if populate:
        cmd += ["-d", stage]
    cmd.append(disk)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        log("mke2fs failed:\n" + r.stdout + r.stderr)
        return False

    if journal_csum:
        # The filesystem lives at PART_OFFSET inside the disk image, and
        # e2fsprogs can only read a filesystem at offset 0 -- so the patch is
        # made on a carved-out copy and written back over the same range.
        scratch = os.path.join(WORK, "stage-csum.img")
        extract_partition(disk, scratch)
        try:
            enable_journal_csum(scratch, journal_csum)
        except Exception as exc:
            log(f"could not enable journal {journal_csum}: {exc}")
            return False
        with open(disk, "r+b") as fd, open(scratch, "rb") as src:
            fd.seek(PART_OFFSET)
            while True:
                chunk = src.read(1 << 20)
                if not chunk:
                    break
                fd.write(chunk)
        os.unlink(scratch)
    return True


def extract_partition(disk, out):
    """Carve the filesystem out so e2fsprogs can read it at offset 0."""
    size = os.path.getsize(disk) - PART_OFFSET
    with open(disk, "rb") as fi, open(out, "wb") as fo:
        fi.seek(PART_OFFSET)
        left = size
        while left > 0:
            chunk = fi.read(min(1 << 20, left))
            if not chunk:
                break
            fo.write(chunk)
            left -= len(chunk)
    return os.path.getsize(out)


# ───────────────────────────── guest execution ─────────────────────────────

class Guest:
    def __init__(self, disk, logfile):
        self.disk = disk
        self.logfile = logfile
        self.buf = b""
        self.raw = b""

    def boot(self):
        m, s = pty.openpty()
        # The host pty must be raw, otherwise the guest never sees our bytes
        # as individual keystrokes.
        tty.setraw(s)
        argv = QEMU + ["-drive",
                       f"file={self.disk},format=raw,if=ide,index=0"]
        self.proc = subprocess.Popen(argv, stdin=s, stdout=s, stderr=s,
                                     close_fds=True)
        os.close(s)
        self.master = m
        return self.wait_for(PROMPT, BOOT_TIMEOUT, "boot")

    def pump(self, sec):
        dl = time.time() + sec
        while time.time() < dl:
            r, _, _ = select.select([self.master], [], [], 0.2)
            if not r:
                continue
            try:
                d = os.read(self.master, 65536)
            except OSError:
                return
            if not d:
                return
            self.buf += d
            self.raw += d
            self.logfile.write(d)
            self.logfile.flush()

    def wait_for(self, marker, timeout, what):
        dl = time.time() + timeout
        while time.time() < dl:
            self.pump(0.3)
            if marker in self.buf:
                i = self.buf.index(marker)
                self.buf = self.buf[i + len(marker):]
                return True
        log(f"  !! {what}: {marker!r} not seen within {timeout}s")
        return False

    def send(self, data):
        os.write(self.master, data)

    def cmd(self, line, markers, timeout=STEP_TIMEOUT):
        """Type a shell command and wait for each marker in order."""
        self.send(line.encode() + b"\n")
        return self.wait_all(markers, timeout)

    def wait_all(self, markers, timeout):
        for mk in markers:
            if not self.wait_for(mk, timeout, "marker"):
                return False, mk
        return True, None

    def kill(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=10)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass


# ───────────────────────────── host-side verification ─────────────────────────

def fsck(part, fix=False):
    """Run a structural check. Returns (rc, output).

    fix=False is the read-only form used for every pass/fail verdict, so a
    check can never quietly repair the very thing it is meant to judge.
    fix=True is used only by journal_recovery_restore(), which has to give
    e2fsck the chance to replay the journal; there the repair is the point.
    """
    args = ["e2fsck", "-fy" if fix else "-fn", part]
    r = subprocess.run(args, capture_output=True, text=True, errors="replace")
    return r.returncode, r.stdout + r.stderr


def debugfs(part, request):
    # errors="replace" matters: a corrupted filesystem makes debugfs emit raw
    # block bytes, and a strict UTF-8 decode of those aborts the whole run --
    # losing the report for the failure that actually mattered.
    r = subprocess.run(["debugfs", "-R", request, part],
                       capture_output=True, text=True, errors="replace")
    return r.stdout + r.stderr


JBD2_MAGIC = 0xC03B3998
JBD2_DESCRIPTOR, JBD2_COMMIT, JBD2_SB_V2 = 1, 2, 4


def journal_activity(part):
    """Count real JBD2 transaction blocks in the journal. Returns (blocks, note).

    This exists because a filesystem can pass every other check here while
    having no journal at all.  The driver shipped a complete JBD2
    implementation that nothing called: writes went straight to the disk, the
    journal stayed exactly as mke2fs left it, and e2fsck was clean because
    there was never anything to replay.  "e2fsck is happy" cannot tell that
    apart from a working journal, so the log has to be inspected directly.

    Walks the real on-disk structures -- superblock -> group descriptor ->
    inode table -> journal inode -- rather than trusting dumpe2fs, because the
    point is to see the bytes the driver actually produced.
    """
    with open(part, "rb") as f:
        def u16(off, base=0):
            f.seek(base + off)
            return struct.unpack_from("<H", f.read(2))[0]

        def u32(off, base=0):
            f.seek(base + off)
            return struct.unpack_from("<I", f.read(4))[0]

        if u16(0x38, 1024) != 0xEF53:
            return 0, "no ext2 magic"

        journal_inum = u32(0xE0, 1024)
        if journal_inum == 0:
            return 0, "no journal (journal_inum is 0)"

        block_size = 1024 << u32(0x18, 1024)
        inodes_per_group = u32(0x28, 1024)
        inode_size = u16(0x58, 1024) or 128
        first_data = u32(0x14, 1024)

        g = (journal_inum - 1) // inodes_per_group
        idx = (journal_inum - 1) % inodes_per_group
        f.seek((first_data + 1 + g) * block_size + 8)
        inode_table = struct.unpack("<I", f.read(4))[0]
        f.seek(inode_table * block_size + idx * inode_size + 0x28)
        jblocks = list(struct.unpack("<15I", f.read(60)))

        found = []
        for n, blk in enumerate(jblocks):
            if not blk:
                continue
            f.seek(blk * block_size)
            hdr = f.read(12)
            magic, btype, _seq = struct.unpack(">III", hdr)
            if magic == JBD2_MAGIC and btype in (JBD2_DESCRIPTOR, JBD2_COMMIT,
                                                 JBD2_SB_V2):
                # The journal superblock is always block 0 of the journal and is
                # written by mke2fs, so it proves nothing about the driver.
                if n == 0 and btype == JBD2_SB_V2:
                    continue
                found.append((blk, btype))

    if not found:
        return 0, (f"journal inode {journal_inum} present but the log holds no "
                   f"descriptor or commit block -- writes are not journalled")
    desc = sum(1 for _, t in found if t == JBD2_DESCRIPTOR)
    comm = sum(1 for _, t in found if t == JBD2_COMMIT)
    return len(found), f"{desc} descriptor + {comm} commit block(s)"


# JBD2 feature bits (include/linux/jbd2.h)
JBD2_INCOMPAT_REVOKE   = 0x1
JBD2_INCOMPAT_64BIT    = 0x2
JBD2_INCOMPAT_ASYNC    = 0x4
JBD2_INCOMPAT_CSUM_V2  = 0x8
JBD2_INCOMPAT_CSUM_V3  = 0x10

# journal block types (include/linux/jbd2.h).  Only a few of these ever appear
# in a log this driver writes, but the reader still has to know them: a
# superblock or revoke block between a transaction's data blocks and its commit
# block is legal, and misreading one as data throws the walk off by a block --
# which then reports every later checksum as wrong.
JBD2_DESCRIPTOR         = 1
JBD2_COMMIT             = 2
JBD2_SUPERBLOCK_V1      = 3
JBD2_SUPERBLOCK_V2      = 4
JBD2_REVOKE             = 5
JBD2_FAST_COMMIT        = 6

JBD2_FLAG_ESCAPE       = 0x1
JBD2_FLAG_SAME_UUID    = 0x2
JBD2_FLAG_DELETED      = 0x4
JBD2_FLAG_LAST_TAG     = 0x8
JBD2_CRC32C_CHKSUM     = 4


def journal_tag_bytes(sz, feat):
    """Port of journal_tag_bytes() in fs/jbd2/journal.c.

    Transcribed from the kernel rather than from memory, because getting it
    wrong changes where every subsequent tag is read from -- and because a
    plausible-but-wrong version of this function is exactly how the driver's
    own tag layout went unexamined.
    """
    if feat & JBD2_INCOMPAT_CSUM_V3:
        return 16
    if feat & JBD2_INCOMPAT_64BIT:
        if feat & JBD2_INCOMPAT_CSUM_V2:
            return 14
        return 12
    if feat & JBD2_INCOMPAT_CSUM_V2:
        return 10
    return 8


# ── CRC32C, and building a checksummed journal to test the driver against ────
#
# JBD2's checksum under csum_v2/csum_v3.  The convention is the part that is
# easy to get wrong, so note it: reflected, polynomial 0x82f63b78, and the
# caller's value is the running CRC verbatim -- no pre-inversion, no final xor.
# That is the opposite of the CRC32 the rest of the kernel expects, and
# jbd2_superblock_csum() genuinely does start from ~0.
#
# Verified two ways, both against e2fsprogs rather than against itself:
#   * e2fsprogs ships its own unit test (lib/ext2fs/crc32c.c test_crc32c) with
#     128 (seed, offset, length, expected) vectors over a fixed buffer.  This
#     reproduces all 128 -- see crc32c_kat_ok() below.
#   * e2fsck accepted a journal superblock whose s_checksum this computed,
#     reported the same value back through dumpe2fs, and then refused to replay
#     the same journal once each of the four checksums was deliberately
#     corrupted.  scripts/jbd2_csum_ref.py does that end to end.

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


def crc32c_kat():
    """Run e2fsprogs' own CRC32C test vectors against *this file's* crc32c.

    The vectors live in jbd2_csum_ref.py, which is the canonical copy; only the
    data is imported, not the implementation, so this actually tests the code
    that builds the fixture below.  Returns (passed, total), or (0, 0) if the
    vector file is missing.

    Run as a step rather than asserted in a comment: this is the one thing the
    whole checksummed-journal path rests on, and a comment claiming it was
    verified cannot fail.
    """
    import importlib.util
    ref = os.path.join(HERE, "jbd2_csum_ref.py")
    if not os.path.exists(ref):
        return 0, 0
    spec = importlib.util.spec_from_file_location("jbd2_csum_ref", ref)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    buf = mod._kat_buf()
    passed = 0
    for seed, off, length, want in mod._KAT_VECTORS:
        if crc32c(seed, buf[off:off + length]) == want:
            passed += 1
    return passed, len(mod._KAT_VECTORS)


# journal superblock field offsets, from include/linux/jbd2.h
JBD2_SB_SEQ = 0x18
JBD2_SB_START = 0x1C
JBD2_SB_COMPAT = 0x24
JBD2_SB_INCOMPAT = 0x28
JBD2_SB_UUID = 0x30
JBD2_SB_CSUM_TYPE = 0x50
JBD2_SB_CHECKSUM = 0xFC
JBD2_SB_SIZE = 0x400          # sizeof(journal_superblock_t)


def journal_sb_csum(sb):
    """jbd2_superblock_csum(): crc32c(~0, sb, sizeof(...)) with s_checksum zero."""
    zeroed = bytearray(sb)
    struct.pack_into(">I", zeroed, JBD2_SB_CHECKSUM, 0)
    return crc32c(0xFFFFFFFF, bytes(zeroed[:JBD2_SB_SIZE]))


def journal_sb_first_block(f):
    """Filesystem block number of the journal inode's first data block.

    Walks superblock -> group descriptor -> inode table -> journal inode, the
    same chain journal_activity() uses.  `imap` is no use here: it reports where
    the inode is stored, not where the file's data begins.
    """
    def u16(off):
        f.seek(off); return struct.unpack_from("<H", f.read(2))[0]
    def u32(off):
        f.seek(off); return struct.unpack_from("<I", f.read(4))[0]

    jinum = u32(1024 + 0xE0)
    if not jinum:
        raise RuntimeError("no journal inode")
    bs = 1024 << u32(1024 + 0x18)
    ipg = u32(1024 + 0x28)
    isize = u16(1024 + 0x58) or 128
    first_data = u32(1024 + 0x14)
    g = (jinum - 1) // ipg
    idx = (jinum - 1) % ipg
    f.seek((first_data + 1 + g) * bs + 8)
    itable = struct.unpack("<I", f.read(4))[0]
    f.seek(itable * bs + idx * isize + 0x28)
    return struct.unpack("<I", f.read(4))[0], bs


def enable_journal_csum(part, mode):
    """Rewrite the journal superblock to advertise a checksummed journal.

    A conformant csum_v2/v3 journal has to satisfy three things at once, and
    getting any of them wrong makes the kernel refuse the mount outright:

      * s_feature_incompat carries CSUM_V2 (or CSUM_V3)
      * s_checksum_type is JBD2_CRC32C_CHKSUM -- a u8 at 0x50, and the kernel
        rejects anything else ("JBD2: Unknown checksum type")
      * s_checksum matches, over the whole 1024-byte superblock

    mode is "v2", "v3" or "v2+64bit".  Returns a human-readable description of
    what it wrote, for the test report.
    """
    feats = {"v2": JBD2_INCOMPAT_CSUM_V2,
             "v3": JBD2_INCOMPAT_CSUM_V3,
             "v2+64bit": JBD2_INCOMPAT_CSUM_V2 | JBD2_INCOMPAT_64BIT}
    if mode not in feats:
        raise ValueError(f"unknown journal mode {mode!r}")

    with open(part, "r+b") as f:
        jblk, bs = journal_sb_first_block(f)
        f.seek(jblk * bs)
        sb = bytearray(f.read(JBD2_SB_SIZE))
        if len(sb) < JBD2_SB_SIZE:
            raise RuntimeError(f"journal superblock at block {jblk} is short")

        magic, = struct.unpack_from(">I", sb, 0)
        if magic != JBD2_MAGIC:
            raise RuntimeError(f"no journal magic at block {jblk}: 0x{magic:08x}")

        struct.pack_into(">I", sb, JBD2_SB_INCOMPAT,
                         struct.unpack_from(">I", sb, JBD2_SB_INCOMPAT)[0] | feats[mode])
        sb[JBD2_SB_CSUM_TYPE] = JBD2_CRC32C_CHKSUM
        struct.pack_into(">I", sb, JBD2_SB_CHECKSUM, journal_sb_csum(sb))
        f.seek(jblk * bs)
        f.write(bytes(sb))

        new = struct.unpack_from(">I", sb, JBD2_SB_INCOMPAT)[0]
        return (f"{mode}: s_feature_incompat=0x{new:08x} "
                f"s_checksum_type={JBD2_CRC32C_CHKSUM} "
                f"s_checksum=0x{struct.unpack_from('>I', sb, JBD2_SB_CHECKSUM)[0]:08x} "
                f"(journal block {jblk})")


def journal_block_map(part, ino=8):
    """Filesystem block number of each journal block, indexed by journal block.

    debugfs rather than a hand-rolled walk, for two reasons.  Reading only
    i_block[12] covers blocks 0..3084 and runs out there, which looks like "the
    journal ends here" rather than like a helper that ran out of pointers; and
    debugfs already knows the extent format.  Its output is *compacted*, though,
    which is its own trap:

        (0-11):522-533, (IND):534, (12-267):535-790, (DIND):791, ...

    so a naive `(\\d+):(\\d+)` per line finds nothing.  The ranges have to be
    expanded.  (IND)/(DIND) entries are the indirection blocks themselves and
    are not part of the data sequence.
    """
    out = debugfs(part, f"stat <{ino}>")
    body, seen = "", False
    for line in out.splitlines():
        s = line.strip()
        if s == "BLOCKS:":
            seen = True
            continue
        if seen:
            if s.startswith("TOTAL") or not s:
                break
            body += s
    if not body:
        return []

    blk = []
    for item in body.split(","):
        item = item.strip()
        m = re.fullmatch(r"\((\d+)-(\d+)\):(\d+)(?:-(\d+))?", item)
        if not m:
            continue                      # (IND):n / (DIND):n
        first, last = int(m.group(1)), int(m.group(2))
        pfirst = int(m.group(3))
        plast = int(m.group(4)) if m.group(4) else pfirst
        for k, logical in enumerate(range(first, last + 1)):
            while len(blk) <= logical:
                blk.append(0)
            blk[logical] = pfirst + k
        if plast != pfirst + (last - first):
            raise RuntimeError(f"non-contiguous block range {item!r}")
    return blk


class JournalFormatError(Exception):
    """The on-disk bytes are not a JBD2 log this reader can parse.

    Distinct from "there is nothing committed": that is an ordinary end state,
    this is a format defect, and the message has to name it.
    """


class Journal:
    """Independent reader for an on-disk JBD2 log.

    Parsed from the format -- fs/jbd2/recovery.c -- and deliberately *not* from
    the driver's writer.  A parser written to match the writer would agree with
    a broken driver and prove nothing, which is the whole reason these checks
    exist.

    One reader serves every check that has to understand the log.  There were
    once two, and they drifted: both of them assumed one descriptor block per
    transaction, which stops being true the moment a transaction has more tags
    than fit in a single descriptor.  A full staging area is JBD2_MAX_TAGS (64)
    tags, and on a csum_v3 journal a 1 KiB descriptor holds only 62 of them, so
    a 64-tag transaction spans *two* descriptor blocks followed by all 64 data
    blocks.  Both copies read the second descriptor as if it were a data block,
    and both then reported 61 of 62 tag checksums as wrong -- a failure that
    looks exactly like a write-path bug and is not one.

    So the shape is the kernel's (fs/jbd2/recovery.c:jbd2_do_replay, and
    do_one_pass for the descriptor scan):

        descriptor*  data*  [revoke*]  commit

    Steps are driven by *count*, not by sniffing for a magic, because a data
    block is a raw copy of a home block and can perfectly well start with
    JBD2_MAGIC by coincidence.  Only a transaction that reaches its commit block
    is ever returned, so an uncommitted trailing transaction -- the normal end
    state, since the driver was interrupted partway through -- can never be
    mistaken for a corrupt one.  Two earlier versions of these checks got that
    wrong and reported phantom defects against perfectly good checksums.
    """

    def __init__(self, part):
        self.part = part
        with open(part, "rb") as f:
            def u16(off, base=0):
                f.seek(base + off); return struct.unpack_from("<H", f.read(2))[0]
            def u32(off, base=0):
                f.seek(base + off); return struct.unpack_from("<I", f.read(4))[0]

            if u16(0x38, 1024) != 0xEF53:
                raise JournalFormatError("no ext2 magic")
            self.jinum = u32(0xE0, 1024)
            if not self.jinum:
                raise JournalFormatError("journalless image")
            self.bs = 1024 << u32(0x18, 1024)
            self.blocks_count = u32(0x04, 1024)
            self.ipg = u32(0x28, 1024)
            isize = u16(0x58, 1024) or 128
            first_data = u32(0x14, 1024)
            self.bpg = u32(0x20, 1024)

            # Blocks whose corruption makes e2fsck stop trusting the primary
            # superblock.  It falls back to the backup copies and then rebuilds
            # the filesystem from them, which *restores* damaged blocks without
            # the journal ever being read.  Any check that wants to prove the
            # journal works has to leave these alone -- see
            # journal_recovery_restore().
            groups = 0
            if self.bpg:
                groups = (self.blocks_count - first_data + self.bpg - 1) // self.bpg
            self.structural = set(range(0, 8)) | {
                first_data + 1 + g * self.bpg for g in range(groups)}

            g = (self.jinum - 1) // self.ipg
            idx = (self.jinum - 1) % self.ipg
            f.seek((first_data + 1 + g) * self.bs + 8)
            itable = struct.unpack("<I", f.read(4))[0]
            f.seek(itable * self.bs + idx * isize + 0x28)
            jsuper = struct.unpack("<I", f.read(4))[0]

        self.jmap = journal_block_map(part, self.jinum)
        if len(self.jmap) < 8:
            raise JournalFormatError(
                f"journal block map has only {len(self.jmap)} blocks")

        with open(part, "rb") as f:
            f.seek(jsuper * self.bs)
            jsb = f.read(self.bs)
        magic, = struct.unpack_from(">I", jsb, 0)
        if magic != JBD2_MAGIC:
            raise JournalFormatError(
                f"journal magic 0x{magic:08x} at block {jsuper}")
        self.jsuper_block = jsuper
        self.sb = jsb
        self.jmaxlen, self.jfirst = struct.unpack_from(">II", jsb, 16)
        self.jfeat, = struct.unpack_from(">I", jsb, 40)
        self.tbytes = journal_tag_bytes(self.bs, self.jfeat)
        self.seed = crc32c(0xFFFFFFFF, jsb[JBD2_SB_UUID:JBD2_SB_UUID + 16])

        # journal_tag_bytes() and count_tags(), transcribed.  Note that the tag
        # walk stops short of the descriptor tail on a checksummed journal,
        # because sizeof(struct jbd2_journal_block_tail) is 4, not 8.
        self.checksummed = bool(self.jfeat & (JBD2_INCOMPAT_CSUM_V2 |
                                              JBD2_INCOMPAT_CSUM_V3))
        self.csum_v3 = bool(self.jfeat & JBD2_INCOMPAT_CSUM_V3)
        self.tail_at = self.bs - 4
        self.tag_limit = self.bs - 4 if self.checksummed else self.bs

    def variant(self, expected):
        """Assert the on-disk journal really is `expected`; return a description.

        The csum variants produce the *same* replay geometry: the same log block
        numbers, the same tag count, the same list of restored blocks.  So a
        green run of `--journal-csum v3` is indistinguishable in the report from
        a green run of plain ext3 -- which is a check that can pass for the
        wrong reason.  If enable_journal_csum() silently did nothing, every step
        after it would still be green and the v2/v3/v2+64bit rows of the matrix
        would all be the same test wearing three different labels.

        So the journal's feature bits are asserted against what the variant
        requires, the tag size the reader derived from them is asserted against
        what that variant implies, and the description goes into the report so a
        reader can see which format was actually exercised.

        The tag sizes are not redundant with the feature check: they are what
        every subsequent tag offset is computed from, so a wrong tag size
        silently shifts the whole tag walk rather than failing loudly.
        """
        want = {
            None:       (0, 8),
            "v2":       (JBD2_INCOMPAT_CSUM_V2, 10),
            "v3":       (JBD2_INCOMPAT_CSUM_V3, 16),
            "v2+64bit": (JBD2_INCOMPAT_64BIT | JBD2_INCOMPAT_CSUM_V2, 14),
        }
        if expected not in want:
            raise JournalFormatError(f"unknown journal csum variant "
                                     f"{expected!r}")
        need, tbytes = want[expected]
        if self.jfeat != need:
            raise JournalFormatError(
                f"journal features are 0x{self.jfeat:08x} but variant "
                f"{expected or 'v1'!r} needs 0x{need:08x} -- this image is not "
                f"the one that was asked for")
        if self.tbytes != tbytes:
            raise JournalFormatError(
                f"variant {expected or 'v1'!r} implies {tbytes}-byte tags, but "
                f"journal_tag_bytes() derived {self.tbytes} from features "
                f"0x{self.jfeat:08x}")
        names = [n for bit, n in (
            (JBD2_INCOMPAT_REVOKE, "REVOKE"),
            (JBD2_INCOMPAT_64BIT, "64BIT"),
            (JBD2_INCOMPAT_ASYNC, "ASYNC_COMMIT"),
            (JBD2_INCOMPAT_CSUM_V2, "CSUM_V2"),
            (JBD2_INCOMPAT_CSUM_V3, "CSUM_V3"),
        ) if self.jfeat & bit]
        return (f"variant={expected or 'v1'} "
                f"features=0x{self.jfeat:08x} "
                f"({' '.join(names) if names else 'none'}) "
                f"tag={self.tbytes}B bs={self.bs} jmaxlen={self.jmaxlen}")

    # ── primitives ──────────────────────────────────────────────────────

    def read(self, n):
        """Journal block n as bytes, or None if it is not addressable."""
        if n < 0 or n >= len(self.jmap):
            return None
        with open(self.part, "rb") as f:
            f.seek(self.jmap[n] * self.bs)
            d = f.read(self.bs)
        return d if len(d) == self.bs else None

    def parse_tags(self, d, log):
        """(home_block, stored_tag_checksum) for each tag in a descriptor.

        t_flags is read as a be16 at tag+6 for *every* tag size, csum_v3
        included -- even though journal_block_tag3_t::t_flags is a be32 at
        tag+4.  That is not a mistake in the kernel, it is a deliberate
        asymmetry: commit.c writes be32 at +4 while count_tags() and
        jbd2_do_replay() both read be16 at +6.  It is safe because every flag
        value is <= JBD2_FLAG_MASK (0x0F), so a be32 of one lands as
        00 00 00 ff, and bytes 6 and 7 read back as 0x00ff.  Reading a be32 at
        +4 yields the same number, so either works here; reading be16 at +6 is
        what the format actually is.

        The *checksum* does move: csum_v2 keeps a be16 at tag+4, csum_v3 a full
        be32 at tag+12.
        """
        out = []
        off = 12
        first = True
        while off + self.tbytes <= self.tag_limit:
            blk, = struct.unpack_from(">I", d, off)
            flags, = struct.unpack_from(">H", d, off + 6)
            if flags & JBD2_FLAG_ESCAPE:
                break
            if flags & JBD2_FLAG_DELETED:
                blk = 0
            elif blk == 0 or blk >= self.blocks_count:
                # A tag naming block 0 or a block past the end of the
                # filesystem means this is not a JBD2 tag stream.  Say so
                # directly: the alternative is walking into the middle of the
                # block until something looks like a commit header, which
                # produces a baffling "no journal magic" instead of naming the
                # actual defect.
                raise JournalFormatError(
                    f"descriptor at log block {log} does not parse as a JBD2 "
                    f"tag stream: tag at byte {off} names filesystem block "
                    f"{blk}, which is outside 0..{self.blocks_count - 1}. "
                    f"Parsed per the format with a {self.tbytes}-byte stride, "
                    f"so the driver's tag spacing does not match the on-disk "
                    f"format and no real JBD2 reader can replay this journal.")
            if self.csum_v3:
                tcsum, = struct.unpack_from(">I", d, off + 12)
            elif self.checksummed:
                tcsum, = struct.unpack_from(">H", d, off + 4)
            else:
                tcsum = None
            out.append((blk, tcsum))
            off += self.tbytes
            if not (flags & JBD2_FLAG_SAME_UUID):
                off += 16          # the journal UUID follows the first tag
            first = False
            if flags & JBD2_FLAG_LAST_TAG:
                break
        if not out:
            raise JournalFormatError(
                f"descriptor at log block {log} yields no tags")
        return out

    # ── the four checksums ──────────────────────────────────────────────

    def desc_csum(self, d):
        """(stored, computed) for a descriptor block's tail, or None."""
        if not self.checksummed:
            return None
        z = bytearray(d)
        z[self.tail_at:self.tail_at + 4] = b"\0\0\0\0"
        return (struct.unpack_from(">I", d, self.tail_at)[0],
                crc32c(self.seed, bytes(z)))

    def commit_csum(self, d):
        """(stored, computed) for a commit block's h_chksum[0], or None.

        h_chksum[0] is at offset 16 -- three be32 header fields, then two u8
        descriptors and two pad bytes.  h_chksum_type and h_chksum_size stay 0
        even under csum_v2; jbd2_commit_block_csum_set() zeroes them
        explicitly.
        """
        if not self.checksummed:
            return None
        z = bytearray(d)
        z[16:20] = b"\0\0\0\0"
        return struct.unpack_from(">I", d, 16)[0], crc32c(self.seed, bytes(z))

    def tag_csum(self, seq, home, data):
        """The tag checksum the kernel computes for one journalled block.

        crc32c over the be32 sequence, then over the data block.  csum_v2 stores
        only the low 16 bits (jbd2_block_tag_csum_verify compares against
        cpu_to_be16); csum_v3 stores all 32.
        """
        full = crc32c(crc32c(self.seed, struct.pack(">I", seq)), data)
        return (full, full) if self.csum_v3 else (full & 0xFFFF, full & 0xFFFF)

    # ── the walk ────────────────────────────────────────────────────────

    def transactions(self):
        """Yield (seq, first_log, descs, tags, data_logs, commit_log, commit).

        `descs` is a list of (log, stored, computed) tail checksums, `tags` a
        list of (home, stored_tag_csum), and `data_logs` the journal block
        number each tag's data copy lives at -- same order, so data_logs[i]
        belongs to tags[i].

        Only committed transactions are yielded.
        """
        pos = self.jfirst
        while 0 <= pos < self.jmaxlen:
            # 1. One or more descriptor blocks, all carrying the same sequence.
            descs, tags, seq, start = [], [], None, None
            while True:
                d = self.read(pos)
                if d is None:
                    return
                magic, btype, bseq = struct.unpack_from(">III", d, 0)
                if magic != JBD2_MAGIC or btype != JBD2_DESCRIPTOR:
                    break
                if seq is None:
                    seq, start = bseq, pos
                elif bseq != seq:
                    # Not this transaction's descriptor after all.
                    break
                csum = self.desc_csum(d)
                descs.append((pos,) + (csum if csum else (None, None)))
                tags.extend(self.parse_tags(d, pos))
                pos += 1
            if not descs:
                return

            # 2. Exactly one data block per tag, in tag order.  Stepped by
            #    count: a data block carries no journal header, so there is
            #    nothing to sniff for, and its first four bytes are home-block
            #    content that may legitimately equal JBD2_MAGIC.
            data_logs = []
            for i in range(len(tags)):
                if self.read(pos + i) is None:
                    return          # torn write: this transaction never commits
                data_logs.append(pos + i)
            pos += len(tags)

            # 3. Revoke blocks, if the feature is in use, then the commit.
            while True:
                d = self.read(pos)
                if d is None:
                    return
                magic, btype, bseq = struct.unpack_from(">III", d, 0)
                if magic == JBD2_MAGIC and btype == JBD2_REVOKE:
                    pos += 1
                    continue
                break
            if magic != JBD2_MAGIC or btype != JBD2_COMMIT or bseq != seq:
                # Uncommitted: the journal was interrupted, or this is the
                # erased region past the head.  Drop it and stop -- there is
                # nothing after an uncommitted transaction to be trusted.
                return
            csum = self.commit_csum(d)
            yield (seq, start, descs, tags, data_logs, pos,
                   csum if csum else (None, None))
            pos += 1


def journal_csum_verify(part):
    """Recompute every checksum of every *committed* transaction on disk.

    The other checks here judge the journal by what e2fsck makes of it, which
    is only as good as e2fsck choosing to look.  This reads the bytes back and
    verifies all four checksums directly, so a driver that wrote a plausible
    but wrong value is caught even if no reader happened to object.

    Three ways a check like this passes without having done anything, all of
    them closed here:

      * Verifying an **uncommitted** transaction.  Its descriptor may be on disk
        with only some of its data blocks written, so its tag checksums are
        meaningless.  Two earlier versions of this function did exactly that
        and reported phantom defects against correct checksums.
      * Verifying a transaction spanning **several descriptor blocks**.  The
        data blocks follow all of them, so reading the second descriptor as a
        data block misaligns everything after it.
      * **Zero** committed transactions, which verifies trivially.

    Returns (ok, note).  The note carries the counts, which is the evidence
    that the check did real work.
    """
    try:
        j = Journal(part)
    except JournalFormatError as e:
        return False, str(e)

    if not j.checksummed:
        return True, (f"journal has no checksum feature (incompat=0x{j.jfeat:08x}), "
                      f"nothing to verify")

    good = bad = txns = tags_seen = 0
    widest = (0, 0, 0)      # (descriptor blocks, tags, seq) of the widest txn
    failures = []

    def tally(what, stored, calc):
        nonlocal good, bad
        good += stored == calc
        bad += stored != calc
        if stored != calc:
            failures.append(f"{what}: 0x{stored:x} != 0x{calc:x}")

    for (seq, first, descs, tags, data_logs, clog, csum) in j.transactions():
        txns += 1
        if len(descs) > widest[0]:
            widest = (len(descs), len(tags), seq)
        for (log, stored, calc) in descs:
            tally(f"descriptor tail at log block {log}", stored, calc)
        for i, (home, stored) in enumerate(tags):
            tags_seen += 1
            if not home or stored is None:
                continue          # deleted tag: no data block, nothing to cover
            data = j.read(data_logs[i])
            if data is None:
                return False, f"journal block {data_logs[i]} vanished mid-walk"
            _, full = j.tag_csum(seq, home, data)
            want = full if j.csum_v3 else full & 0xFFFF
            tally(f"data for home block {home} at log block {data_logs[i]}",
                  stored, want)
        tally(f"commit at log block {clog}", csum[0], csum[1])

    if txns == 0:
        return False, ("no committed transaction on disk; the checksums were "                       "verified against nothing")
    if bad:
        return False, (f"{bad} of {good + bad} kernel-written checksums do not "
                       f"recompute. First: {failures[0]}")
    note = (f"all {good} checksums across {txns} committed transaction(s) and "
            f"{tags_seen} tag(s) recompute exactly ({j.tbytes}-byte tags, "
            f"seed 0x{j.seed:08x})")
    if widest[0] > 1:
        # Worth calling out: the tag stream only continues across a descriptor
        # boundary correctly if the data blocks are known to come after *all*
        # of them, and a reader that gets that wrong agrees with a broken
        # driver.  So the widest transaction is the one carrying the evidence.
        note += (f"; widest was {widest[1]} tags over {widest[0]} descriptor "
                 f"blocks (seq {widest[2]})")
    return True, note


def journal_recovery_restore(part):
    """Corrupt the home copies of the live journalled transaction, replay, and
    check they come back.  Returns (ok, note).

    This is the only check here that can tell a *working* journal from a
    journal that merely exists.  Every other check is satisfied by a
    filesystem whose home copies were already correct, which is the state the
    driver is always in: it writes the home copies immediately after the
    commit block, so there is normally nothing for recovery to do and a
    correct replay and a no-op are indistinguishable.

    So this manufactures the situation recovery exists for.  It reads a
    transaction the driver actually committed, records the home blocks'
    contents, overwrites them with garbage, and asks e2fsck to recover.  If the
    on-disk format is right the journal copy is written back and the blocks are
    restored; if the format is wrong e2fsck cannot parse the descriptor,
    silently recovers nothing, and -- this is the part that matters -- still
    exits 0 and reports a clean filesystem.

    Two ways this check used to pass without the journal doing anything, both
    now closed:

      * **e2fsck rebuilt the filesystem instead of replaying.**  The first
        version poisoned the group descriptor table along with everything else.
        e2fsck rejected the primary, fell back to the backup copies, and
        reconstructed every inode table block from them -- so the blocks came
        back, the check passed, and the journal was never opened.  The tell is
        the log line "Group descriptors look bad... trying backup blocks", and
        the structural blocks are now excluded from the poison set.
      * **e2fsck replayed the journal but the log was unwalkable.**  The second
        version excluded the structural blocks, which did produce a real
        "recovering journal", and still passed on an image whose replay left an
        empty root directory -- because "the block no longer holds the poison"
        is also true of a block e2fsck overwrote with zeroes while rebuilding
        it.  So the check now also requires that the filesystem around the
        repaired blocks is *intact*: a file that existed before recovery must
        still read back afterwards.  A rebuild clears every inode; a replay
        leaves them alone.

    Returns (False, reason) rather than raising when there is no committed
    transaction to test, so the caller can report "nothing to check" instead of
    a spurious pass.
    """
    work = part + ".recover"
    shutil.copyfile(part, work)

    try:
        j = Journal(work)
    except JournalFormatError as e:
        os.path.exists(work) and os.unlink(work)
        return False, str(e)

    # The last committed transaction is the one a crash would leave for replay.
    live = None
    for txn in j.transactions():
        live = txn
    if live is None:
        os.unlink(work)
        return False, "no committed transaction to recover"

    _seq, first, _descs, tags, _data_logs, clog, _csum = live
    named = sorted({h for h, _ in tags if h})
    home = [b for b in named if b not in j.structural]
    skipped = [b for b in named if b in j.structural]
    if not home:
        os.unlink(work)
        return False, (f"the last committed transaction names only structural "
                       f"block(s) {named}, so there is no crash window to "
                       f"manufacture that e2fsck would answer with a replay "
                       f"rather than a rebuild")

    # A file that must still be readable afterwards, to tell a replay from a
    # rebuild.  Read before anything is touched so the expectation is the
    # driver's own output, not a hardcoded guess.
    sentinel_path, sentinel_want = "/etc/conf.txt", b"alpha content one"
    sentinel_had = sentinel_want in debugfs(work, f"cat {sentinel_path}").encode(
        "utf-8", "replace")

    bs = j.bs
    poison = b"\xde\xad\xbe\xef" * (bs // 4)
    with open(work, "r+b") as f:
        for b in home:
            f.seek(b * bs)
            f.write(poison)

    rc, out = fsck(work, fix=True)

    with open(work, "rb") as f:
        restored, still_bad = [], []
        for b in home:
            f.seek(b * bs)
            (still_bad if f.read(bs) == poison else restored).append(b)
    rebuilt = [ln.strip() for ln in out.splitlines() if "look bad" in ln]
    sentinel_now = debugfs(work, f"cat {sentinel_path}")
    os.unlink(work)

    where = (f"transaction seq {_seq} at log block {first}, committed at log "
             f"block {clog}, {len(tags)} tag(s) over {len(home)} live block(s)")
    if skipped:
        where += f" ({len(skipped)} structural block(s) {skipped} left intact)"

    # A rebuild, not a replay.  Checked first: everything below is meaningless
    # if e2fsck discarded the primary and reconstructed the filesystem.
    if rebuilt:
        return False, (
            f"e2fsck rejected the primary superblock and fell back to the "
            f"backups ({rebuilt[0]}), so it rebuilt the filesystem rather than "
            f"replaying the journal. The {where} was never exercised.")

    if sentinel_had and sentinel_want.decode() not in sentinel_now:
        return False, (
            f"e2fsck replayed the journal but {sentinel_path} no longer reads "
            f"back ({sentinel_now.strip()!r} instead of "
            f"{sentinel_want.decode()!r}), so replay did not reconstruct the "
            f"filesystem it was supposed to. {where.capitalize()}. This is what "
            f"an unwalkable log looks like from the outside: the blocks are "
            f"no longer corrupt, but the metadata that describes them is not "
            f"the driver's.")

    # A checksum complaint is its own failure, distinct from "not restored".
    # e2fsck can restore most of a transaction and still reject one block's
    # checksum, in which case still_bad is empty and the restore check alone
    # would call a journal with a wrong checksum correct.  On a checksummed
    # journal this is the assertion that the driver's *written* checksums are
    # right, because e2fsck recomputes all of them while replaying.
    csum_err = [ln for ln in out.splitlines()
                if "checksum" in ln.lower() and "error" in ln.lower()]
    if csum_err:
        return False, (
            f"e2fsck rejected the driver's journal checksums while replaying: "
            f"{csum_err[0].strip()}. Every checksum it verified is one the "
            f"driver wrote, so this is a write-path defect, not a recovery one.")

    where = (f"transaction seq {_seq} at log block {first}, committed at log "
             f"block {clog}, {len(tags)} tag(s) over {len(home)} live block(s)")
    if still_bad:
        return False, (
            f"e2fsck did not restore {len(still_bad)}/{len(home)} journalled "
            f"block(s) {still_bad} after a crash-window corruption, and still "
            f"exited {rc}. {where.capitalize()}. Tag stream parsed per the "
            f"format with a {j.tbytes}-byte stride; the journal is present but "
            f"its descriptor blocks are not in a form a real JBD2 reader can "
            f"walk.")
    note = (f"e2fsck replay of the {where} restored {len(restored)}/{len(home)} "
            f"journalled block(s) {restored} (rc={rc}")
    if skipped:
        note += f", {len(skipped)} structural block(s) {skipped} not damaged"
    if sentinel_had:
        note += f", {sentinel_path} still intact"
    return True, note + ")"


# ───────────────────────────── the test itself ─────────────────────────────

def run(fstype, keep, journal_csum=None, journal_kb=None):
    os.makedirs(WORK, exist_ok=True)
    disk = os.path.join(WORK, f"disk-{fstype}.img")
    part = os.path.join(WORK, f"part-{fstype}.img")
    serial = os.path.join(WORK, f"serial-{fstype}.log")
    report = os.path.join(WORK, f"report-{fstype}.txt")

    results = []
    lines = []

    def step(ok, label, detail="", evidence=""):
        # `detail` explains a failure and is therefore only worth printing when
        # the step failed.  `evidence` is the positive counterpart: the measured
        # result of a step that passed, kept so a later reader can tell a check
        # that did real work from one that passed vacuously.
        results.append(ok)
        tail = ""
        if evidence:
            tail = f"  ({evidence})"
        elif detail and not ok:
            tail = f"  {detail}"
        lines.append(f"  [{'ok ' if ok else 'FAIL'}] {label}{tail}")
        log(lines[-1])

    # The fixture is built by this script's own CRC32C, so prove that checksum
    # against e2fsprogs before trusting anything derived from it.  A wrong
    # fixture would otherwise be rejected by the kernel at mount, and the
    # rejection would look like a driver defect.
    if journal_csum:
        passed, total = crc32c_kat()
        step(total > 0 and passed == total,
             "CRC32C matches e2fsprogs' own test vectors",
             f"{passed}/{total} vectors matched; the {journal_csum} fixture is "
             f"built with this function, so a mismatch invalidates this run")

    log(f"=== building {fstype} image ===")
    if not build_image(disk, fstype, journal_csum=journal_csum, journal_kb=journal_kb):
        lines.append("mke2fs FAILED")
        open(report, "w").write("\n".join(lines))
        return False
    step(True, f"built {DISK_SIZE_M}M disk with {fstype}"
               + (f" + journal {journal_csum}" if journal_csum else "")
               + (f" journal={journal_kb}KB" if journal_kb else ""))

    # Confirm the image really is what we think it is, before blaming the kernel.
    rc, out = fsck(extract_partition(disk, part) and part)
    step(rc == 0, "freshly built image passes e2fsck", f"rc={rc}\n{out}")

    if journal_csum:
        # Two things must be true for the kernel to mount this at all, and both
        # are checked by dumpe2fs -- an independent implementation echoing back
        # our own s_checksum is the point.
        d = subprocess.run(["dumpe2fs", "-h", part], capture_output=True, text=True)
        jfeat = next((l.split(":", 1)[1].strip() for l in d.stdout.splitlines()
                      if l.startswith("Journal features")), "")
        jtype = next((l.split(":", 1)[1].strip() for l in d.stdout.splitlines()
                      if l.startswith("Journal checksum type")), "")
        jsum = next((l.split(":", 1)[1].strip() for l in d.stdout.splitlines()
                     if l.startswith("Journal checksum")), "")
        want_feat = {"v2": "journal_checksum_v2",
                     "v3": "journal_checksum_v3",
                     "v2+64bit": "journal_checksum_v2"}.get(journal_csum, "")
        step(want_feat in jfeat and jtype == "crc32c" and jsum != "0",
             f"e2fsprogs sees a crc32c {journal_csum} journal",
             f"features={jfeat!r} type={jtype!r} checksum={jsum!r}",
             evidence=f"{jfeat}, {jtype}, {jsum}")

    log(f"=== booting guest with {fstype} disk ===")
    g = Guest(disk, open(serial, "wb"))
    booted = False
    for attempt in range(1, BOOT_RETRIES + 1):
        if g.boot():
            booted = True
            break
        log(f"  boot attempt {attempt} failed; retrying")
        g.kill()
        time.sleep(2)
    step(booted, "guest booted to shell prompt")
    if not booted:
        open(report, "w").write("\n".join(lines))
        return False

    # The partition is auto-mounted at boot (main.c: codefs, then ext2).
    # NB: the shell's own ls/cat/mkdir go through fs.c, which is the in-memory
    # initramfs node table and never touches the disk driver, so they prove
    # nothing about ext2. Everything below uses ext2-native entry points.
    ok, miss = g.cmd("els /", [b"/"])
    step(ok, "els lists the mounted ext2 root", f"missing {miss!r}")

    ok, miss = g.cmd("ecat /etc/conf.txt", [b"alpha content one"])
    step(ok, "ecat reads a file mke2fs populated", f"missing {miss!r}")

    ok, miss = g.cmd("fstest", [b"FSTEST RESULT"])
    step(ok, "fstest ran to completion", f"missing {miss!r}")

    # Parse the per-check lines out of the captured serial output.
    checks = re.findall(rb"FSTEST (ok|FAIL) (\S+)", g.raw)
    named = [(s.decode(), n.decode()) for s, n in checks]
    failed = [n for s, n in named if s == "FAIL"]
    for state, name in named:
        lines.append(f"       fstest {state:4s} {name}")
    step(bool(named) and not failed,
         f"all {len(named)} ext2 driver checks passed",
         "failed: " + ", ".join(failed) if failed else "no checks reported")

    # Flush everything before we pull the plug, so what we verify is what the
    # kernel committed rather than whatever happened to be in its buffers.
    g.cmd("sync", [PROMPT])
    g.pump(1)
    g.kill()

    log("=== verifying the image from the host ===")
    extract_partition(disk, part)
    rc, out = fsck(part)
    step(rc == 0, "e2fsck clean after guest writes", f"rc={rc}\n{out}")

    ls = debugfs(part, "ls -l /")
    step("fstest_small" in ls, "kernel created /fstest_small on disk", ls)
    step("fstest_big" in ls, "kernel created /fstest_big on disk", ls)
    ls2 = debugfs(part, "ls -l /fstest_dir")
    step("File not found" in ls2, "kernel removed /fstest_dir again", ls2)

    st = debugfs(part, "stat /fstest_big")
    m = re.search(r"Size:\s+(\d+)", st)
    step(bool(m) and int(m.group(1)) == 20480,
         "big file has the right size on disk", st)

    cat = debugfs(part, "cat /etc/conf.txt")
    step("alpha content one" in cat,
         "pre-populated file still intact", cat)

    # Only ext3 has a journal to check. An ext2 image is journalless by
    # definition, so asking for transaction blocks there would be a false
    # failure rather than a finding.
    if fstype != "ext2":
        nblocks, note = journal_activity(part)
        step(nblocks > 0, "writes reached the JBD2 journal", note)

        # Read the variant once; both the recovery and checksum steps
        # below depend on the journal being the one that was asked for,
        # and none of them distinguishes the three formats on their own.
        j = Journal(part)
        _vd = j.variant(journal_csum)
        step(True, f"journal is the {journal_csum or 'v1'} format",
             evidence=_vd)

        # EXT3_FEATURE_INCOMPAT_RECOVER = 0x0004 in s_feature_incompat, which
        # lives at byte 1024 + 0x60 of the partition.
        #
        # e2fsck only *replays* a journal when this bit is set.  With it clear
        # e2fsck treats the filesystem as cleanly unmounted and clears the
        # journal instead, which discards any committed-but-not-checkpointed
        # transaction and leaves its blocks half-old, half-new.  CodeOS has no
        # unmount path, so the bit must always be set after a journalled write.
        with open(part, "rb") as fh:
            fh.seek(1024 + 0x60)
            incompat = struct.unpack("<I", fh.read(4))[0]
        step(bool(incompat & 0x0004),
             "superblock is marked as needing journal recovery",
             f"feature_incompat=0x{incompat:08x} RECOVER(0x4) "
             f"{'set' if incompat & 0x0004 else 'CLEAR'}")

        # The decisive journal check. Everything above it is satisfied by a
        # journal that exists but cannot be read, because the driver writes
        # the home copies right after the commit block, so there is normally
        # nothing left for recovery to do.
        ok, note = journal_recovery_restore(part)
        step(ok, "e2fsck can actually replay the journal", note,
             evidence=note if ok else "")

        # Read the checksums back and recompute them, rather than leaving it to
        # whether e2fsck happened to object. On a v1 journal this reports that
        # there is nothing to verify, which is the honest answer.
        ok, note = journal_csum_verify(part)
        step(ok, "kernel-written journal checksums recompute", note,
             evidence=note if ok else "")

    panic = b"OWPANIC" in g.raw
    pf = b"!!! PF at" in g.raw
    step(not panic, "no OWPANIC in serial log")
    step(not pf, "no page fault in serial log")

    # linux-probe is a real assertion now: pass() checks the return value and
    # the program exits nonzero if any check failed, so this catches syscall
    # regressions that used to print "OK" unconditionally.  Genuine stubs
    # (ENOSYS) are reported as LP[GAP] and deliberately not counted here.
    m = re.search(rb"LPROBE: checks=(\d+) failed=(\d+)", g.raw)
    fails = re.findall(rb"LP\[FAIL\] ([^\n:]+)", g.raw)
    if m:
        nchecks, nfailed = int(m.group(1)), int(m.group(2))
        step(nfailed == 0,
             f"linux-probe: all {nchecks} syscall assertions pass",
             f"failed={nfailed}"
             + (f" first={fails[0].decode(errors='replace')}" if fails else ""),
             evidence=", ".join(f.decode(errors="replace") for f in fails))
    else:
        step(False, "linux-probe ran and reported a summary",
             "no 'LPROBE: checks=' line in serial log")

    allok = all(results)
    lines.insert(0, f"VERDICT: {'ALL STEPS PASSED' if allok else 'FAILURES PRESENT'}"
                    f"  ({fstype})")
    open(report, "w").write("\n".join(lines) + "\n")
    log("")
    log(f"VERDICT: {'ALL STEPS PASSED' if allok else 'FAILURES PRESENT'} ({fstype})")
    log(f"report: {report}")

    if not keep:
        for f in (disk, part):
            try:
                os.remove(f)
            except OSError:
                pass
    return allok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fstype", default="ext3",
                    choices=["ext2", "ext3", "ext4"])
    ap.add_argument("--keep", action="store_true",
                    help="keep the disk image for inspection")
    ap.add_argument("--journal-kb", type=int, default=None,
                    help="journal size in kilobytes (passed as -J size=N "
                         "to mke2fs).  A small journal exercises the JBD2 "
                         "ring-wrap path sooner than the default 4MB.  "
                         "Must be a multiple of 1024 and >= 1024.")
    ap.add_argument("--journal-csum", default=None,
                    choices=["v2", "v3", "v2+64bit"],
                    help="build the journal superblock to advertise a "
                         "checksummed journal. mke2fs here cannot do this "
                         "(its /etc/mke2fs.conf has no [journal] section and "
                         "this build ignores MKE2FS_CONFIG), so fs_test.py "
                         "rewrites it and dumpe2fs/e2fsck remain the authority.")
    args = ap.parse_args()
    if not os.path.exists(ISO):
        log(f"missing {ISO} -- build it with: make -C {KERNEL_DIR} codeos-1.0.iso")
        return 2
    if args.journal_csum and args.fstype == "ext2":
        log("ext2 has no journal; --journal-csum would test nothing")
        return 2
    if args.journal_kb and args.fstype == "ext2":
        log("ext2 has no journal; --journal-kb would test nothing")
        return 2
    return 0 if run(args.fstype, args.keep, args.journal_csum, args.journal_kb) else 1


if __name__ == "__main__":
    sys.exit(main())
