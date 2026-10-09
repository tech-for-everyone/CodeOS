#!/usr/bin/env python3
"""mkcodefs.py — Create a CodeFS filesystem image.

Formats a raw disk image with the CodeOS-exclusive CoW filesystem.
CodeFS features: CRC32 checksums, extent-based allocation, inline data,
simple journal, copy-on-write semantics.

Usage:
    python3 scripts/mkcodefs.py disk.img                   # 16 MB default
    python3 scripts/mkcodefs.py disk.img --size 64M        # custom size
    python3 scripts/mkcodefs.py disk.img --size 256M       # larger
    python3 scripts/mkcodefs.py disk.img --root-dir ./root  # populate from dir

CodeFS disk layout (4096-byte blocks):
    Block 0:  Reserved (boot sector)
    Block 1:  Superblock
    Block 2:  Block bitmap
    Block 3:  Inode bitmap
    Blocks 4+: Inode table
    Then:     Journal area
    Then:     Data blocks (extents stored here)
"""

import argparse
import math
import os
import struct
import sys
import time

# ── Constants ────────────────────────────────────────────────

CODEFS_MAGIC = 0x434F4445  # "CODE"
CODEFS_VERSION = 1
CODEFS_BLOCK_SIZE = 4096
CODEFS_INODE_SIZE = 256
CODEFS_INLINE_MAX = 60
CODEFS_MAX_EXTENTS = 4
CODEFS_NAME_MAX = 255
CODEFS_JOURNAL_BLOCKS = 256

CODEFS_FT_UNKNOWN = 0
CODEFS_FT_REG = 1
CODEFS_FT_DIR = 2
CODEFS_FT_SYMLINK = 3


# ── CRC32 ────────────────────────────────────────────────────

def crc32_table():
    tbl = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        tbl.append(c)
    return tbl

CRC_TABLE = crc32_table()

def crc32(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc = CRC_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


# ── Struct packing ───────────────────────────────────────────

def pack_superblock(sb):
    """Pack 512-byte superblock (padded to fill exactly 512 bytes)."""
    fmt = '<IIIIIIIIIIIIIIIIIIIIII'
    parts = struct.pack(fmt,
        sb['magic'],           # 4
        sb['version'],         # 4
        sb['flags'],           # 4
        sb['block_size'],      # 4
        sb['blocks_count'],    # 4
        sb['free_blocks'],     # 4
        sb['inodes_count'],    # 4
        sb['free_inodes'],     # 4
        sb['root_inode'],      # 4
        sb['journal_inode'],   # 4
        sb['journal_start'],   # 4
        sb['journal_len'],     # 4
        sb['inode_table_start'],  # 4
        sb['data_area_start'],    # 4
        sb['inode_count'],     # 4
        sb['total_inodes'],    # 4
        sb['mount_count'],     # 4
        sb['max_mounts'],      # 4
        sb['state'],           # 4
        sb['created_time'],    # 4
        sb['last_mount'],      # 4
        sb['last_write'],      # 4
    )
    # pad to 444 bytes (before CRC)
    parts += b'\x00' * (444 - len(parts))
    # CRC placeholder
    parts += struct.pack('<I', 0)
    # pad to 512 bytes
    parts += b'\x00' * (512 - len(parts))
    return parts

def pack_inode(ino):
    """Pack 256-byte inode."""
    data = struct.pack('<HHHIIIIIII',
        ino['mode'], ino['uid'], ino['gid'], ino['size'],
        ino['atime'], ino['ctime'], ino['mtime'], ino['links'],
        ino['flags'], ino['inline_len']
    )
    inline = ino.get('inline_data', b'\x00' * CODEFS_INLINE_MAX)
    data += inline[:CODEFS_INLINE_MAX]
    if len(inline) < CODEFS_INLINE_MAX:
        data += b'\x00' * (CODEFS_INLINE_MAX - len(inline))
    # extents (4 x 16 bytes)
    extents = ino.get('extents', [])
    for e in extents[:CODEFS_MAX_EXTENTS]:
        data += struct.pack('<IIII', e[0], e[1], e[2], 0)
    for _ in range(CODEFS_MAX_EXTENTS - len(extents)):
        data += b'\x00' * 16
    data += struct.pack('<I', ino.get('extent_count', 0))
    data += b'\x00' * 8  # pad
    data += struct.pack('<I', 0)  # CRC placeholder
    # pad to 256 bytes
    data += b'\x00' * (CODEFS_INODE_SIZE - len(data))
    assert len(data) == CODEFS_INODE_SIZE, f"Inode size {len(data)} != {CODEFS_INODE_SIZE}"
    return data

def pack_dirent(inode_num, name, file_type):
    """Pack a directory entry."""
    name_bytes = name.encode('utf-8')[:CODEFS_NAME_MAX]
    rec_len = 8 + len(name_bytes)
    rec_len = (rec_len + 3) & ~3  # align to 4
    data = struct.pack('<IHBB', inode_num, rec_len, len(name_bytes), file_type)
    data += name_bytes
    data += b'\x00' * (rec_len - len(data))
    return data


# ── Image builder ────────────────────────────────────────────

class CodeFSImage:
    def __init__(self, size_bytes):
        self.size = size_bytes
        self.block_count = size_bytes // CODEFS_BLOCK_SIZE
        self.data = bytearray(size_bytes)
        self.inode_counter = 2  # 1 = root

        # Layout calculation
        self.inode_table_start = 4  # blocks 0-3: boot, sb, bbmp, ibmp
        inode_table_blocks = 16  # room for 16*4096/128 = 512 inodes
        journal_start = self.inode_table_start + inode_table_blocks
        self.data_area_start = journal_start + CODEFS_JOURNAL_BLOCKS

        # Bitmaps (each 1 block = 4096 bytes = 32768 bits)
        self.block_bitmap = bytearray(CODEFS_BLOCK_SIZE)
        self.inode_bitmap = bytearray(CODEFS_BLOCK_SIZE)

        # Inode table (in memory)
        self.inodes = {}
        self.root_inode_num = None

        self.now = int(time.time())

    def alloc_inode(self, mode, size=0, uid=0, gid=0):
        """Allocate an inode, return inode number."""
        ino_num = self.inode_counter
        self.inode_counter += 1
        idx = ino_num - 1
        self.inode_bitmap[idx // 8] |= (1 << (idx % 8))
        self.inodes[ino_num] = {
            'mode': mode,
            'uid': uid,
            'gid': gid,
            'size': size,
            'atime': self.now,
            'ctime': self.now,
            'mtime': self.now,
            'links': 1,
            'flags': 0,
            'inline_len': 0,
            'inline_data': b'\x00' * CODEFS_INLINE_MAX,
            'extents': [],
            'extent_count': 0,
        }
        return ino_num

    def alloc_data_block(self):
        """Allocate a data block, return block number."""
        for i in range(len(self.block_bitmap)):
            if self.block_bitmap[i] == 0xFF:
                continue
            for bit in range(8):
                if not (self.block_bitmap[i] & (1 << bit)):
                    self.block_bitmap[i] |= (1 << bit)
                    return self.data_area_start + i * 8 + bit
        return None

    def write_block(self, block_num, data):
        """Write data to a block."""
        off = block_num * CODEFS_BLOCK_SIZE
        d = data[:CODEFS_BLOCK_SIZE]
        self.data[off:off + len(d)] = d

    def read_block(self, block_num):
        off = block_num * CODEFS_BLOCK_SIZE
        return bytes(self.data[off:off + CODEFS_BLOCK_SIZE])

    def write_inode(self, ino_num, ino_data):
        """Write inode to inode table."""
        idx = ino_num - 1
        byte_off = idx * CODEFS_INODE_SIZE
        blk = self.inode_table_start + (byte_off // CODEFS_BLOCK_SIZE)
        off_in_blk = byte_off % CODEFS_BLOCK_SIZE
        packed = bytearray(pack_inode(ino_data))
        # CRC at offset 170 within 256-byte inode
        packed[170:174] = b'\x00\x00\x00\x00'
        computed = crc32(bytes(packed))
        packed[170:174] = struct.pack('<I', computed)
        # Read-modify-write
        existing = bytearray(self.read_block(blk))
        existing[off_in_blk:off_in_blk + CODEFS_INODE_SIZE] = bytes(packed)
        self.write_block(blk, existing)

    def store_file_data(self, ino_num, data):
        """Store file data using extents (or inline for small files)."""
        ino = self.inodes[ino_num]
        if len(data) <= CODEFS_INLINE_MAX:
            ino['inline_data'] = data + b'\x00' * (CODEFS_INLINE_MAX - len(data))
            ino['inline_len'] = len(data)
            ino['size'] = len(data)
            return

        ino['size'] = len(data)
        block_count = (len(data) + CODEFS_BLOCK_SIZE - 1) // CODEFS_BLOCK_SIZE
        written = 0
        extents = []
        for b in range(block_count):
            disk_blk = self.alloc_data_block()
            if disk_blk is None:
                raise RuntimeError("Out of data blocks")
            chunk = data[written:written + CODEFS_BLOCK_SIZE]
            buf = bytearray(CODEFS_BLOCK_SIZE)
            buf[:len(chunk)] = chunk
            self.write_block(disk_blk, buf)
            extents.append((b, 1, disk_blk))
            written += len(chunk)

        # Coalesce adjacent extents
        coalesced = []
        for e in extents:
            if coalesced and coalesced[-1][0] + coalesced[-1][1] == e[0] and coalesced[-1][2] + coalesced[-1][1] == e[2]:
                coalesced[-1] = (coalesced[-1][0], coalesced[-1][1] + 1, coalesced[-1][2])
            else:
                coalesced.append(list(e))

        ino['extents'] = coalesced[:CODEFS_MAX_EXTENTS]
        ino['extent_count'] = len(ino['extents'])

    def add_dir_entry(self, dir_ino, child_ino, name, file_type):
        """Add entry to directory."""
        ino = self.inodes[dir_ino]
        entry = pack_dirent(child_ino, name, file_type)

        if ino['size'] == 0:
            # First entry: allocate a block
            disk_blk = self.alloc_data_block()
            if disk_blk is None:
                raise RuntimeError("Out of data blocks")
            buf = bytearray(CODEFS_BLOCK_SIZE)
            buf[:len(entry)] = entry
            self.write_block(disk_blk, buf)
            ino['extents'] = [(0, 1, disk_blk)]
            ino['extent_count'] = 1
            ino['size'] = CODEFS_BLOCK_SIZE
            return

        # Try to append to existing block
        block_idx = (ino['size'] // CODEFS_BLOCK_SIZE) - 1
        disk_blk = None
        for e in ino['extents']:
            if e[0] <= block_idx < e[0] + e[1]:
                disk_blk = e[2] + (block_idx - e[0])
                break
        if disk_blk is None:
            raise RuntimeError("Cannot find directory block")

        buf = bytearray(self.read_block(disk_blk))
        # Find end of entries
        off = 0
        while off < CODEFS_BLOCK_SIZE:
            if off + 8 > CODEFS_BLOCK_SIZE:
                break
            inode_val = struct.unpack_from('<I', buf, off)[0]
            rec_len = struct.unpack_from('<H', buf, off + 4)[0]
            if inode_val == 0 and rec_len == 0:
                break
            if rec_len == 0:
                break
            off += rec_len

        if off + len(entry) <= CODEFS_BLOCK_SIZE:
            buf[off:off + len(entry)] = entry
            self.write_block(disk_blk, buf)
        else:
            # Need new block
            new_blk = self.alloc_data_block()
            if new_blk is None:
                raise RuntimeError("Out of data blocks")
            new_buf = bytearray(CODEFS_BLOCK_SIZE)
            new_buf[:len(entry)] = entry
            self.write_block(new_blk, new_buf)
            last_extent = ino['extents'][-1]
            ino['extents'][-1] = (last_extent[0], last_extent[1] + 1, last_extent[2])
            ino['size'] += CODEFS_BLOCK_SIZE

    def create_root(self):
        """Create root directory inode with . and .. entries."""
        root_ino = self.alloc_inode(0o40755)
        self.inodes[root_ino]['links'] = 2

        # . entry
        dot = pack_dirent(root_ino, '.', CODEFS_FT_DIR)
        # .. entry
        dotdot = pack_dirent(root_ino, '..', CODEFS_FT_DIR)

        disk_blk = self.alloc_data_block()
        buf = bytearray(CODEFS_BLOCK_SIZE)
        off = 0
        for entry in [dot, dotdot]:
            buf[off:off + len(entry)] = entry
            off += len(entry)
        self.write_block(disk_blk, buf)
        self.inodes[root_ino]['extents'] = [(0, 1, disk_blk)]
        self.inodes[root_ino]['extent_count'] = 1
        self.inodes[root_ino]['size'] = CODEFS_BLOCK_SIZE
        self.root_inode_num = root_ino

        return root_ino

    def populate_from_dir(self, root_path, root_ino):
        """Recursively populate filesystem from a host directory."""
        for entry in sorted(os.listdir(root_path)):
            if entry.startswith('.'):
                continue
            full = os.path.join(root_path, entry)
            if os.path.isdir(full):
                dir_ino = self.alloc_inode(0o40755)
                self.inodes[dir_ino]['links'] = 2
                self.add_dir_entry(root_ino, dir_ino, entry, CODEFS_FT_DIR)
                self.populate_from_dir(full, dir_ino)
            elif os.path.isfile(full):
                with open(full, 'rb') as f:
                    data = f.read()
                file_ino = self.alloc_inode(0o100644)
                self.store_file_data(file_ino, data)
                self.add_dir_entry(root_ino, file_ino, entry, CODEFS_FT_REG)

    def finalize(self):
        """Write all metadata to disk image."""
        if self.root_inode_num is None:
            self.create_root()
        root_ino = self.root_inode_num

        # Write superblock
        sb = {
            'magic': CODEFS_MAGIC,
            'version': CODEFS_VERSION,
            'flags': 0x0001,  # clean
            'block_size': CODEFS_BLOCK_SIZE,
            'blocks_count': self.block_count,
            'free_blocks': self.block_count - self.data_area_start,
            'inodes_count': self.inode_counter - 1,
            'free_inodes': 32768 - (self.inode_counter - 1),
            'root_inode': root_ino,
            'journal_inode': 0,
            'journal_start': self.inode_table_start + 16,
            'journal_len': CODEFS_JOURNAL_BLOCKS,
            'inode_table_start': self.inode_table_start,
            'data_area_start': self.data_area_start,
            'inode_count': self.inode_counter - 1,
            'total_inodes': 32768,
            'mount_count': 0,
            'max_mounts': 1000,
            'state': 0x0001,
            'created_time': self.now,
            'last_mount': 0,
            'last_write': self.now,
        }
        sb_raw = bytearray(pack_superblock(sb))
        sb_crc = crc32(bytes(sb_raw[:444]))
        sb_raw[444:448] = struct.pack('<I', sb_crc)
        self.write_block(1, sb_raw)

        # Write bitmaps
        self.write_block(2, self.block_bitmap)
        self.write_block(3, self.inode_bitmap)

        # Write all inodes
        for ino_num, ino_data in self.inodes.items():
            self.write_inode(ino_num, ino_data)

        # Count free blocks used
        used = 0
        for b in range(self.data_area_start, self.block_count):
            idx = b - self.data_area_start
            if self.block_bitmap[idx // 8] & (1 << (idx % 8)):
                used += 1
        sb['free_blocks'] = self.block_count - self.data_area_start - used

        # Rewrite superblock with final counts
        sb_raw = bytearray(pack_superblock(sb))
        sb_crc = crc32(bytes(sb_raw[:444]))
        sb_raw[444:448] = struct.pack('<I', sb_crc)
        self.write_block(1, sb_raw)

    def save(self, path):
        with open(path, 'wb') as f:
            f.write(self.data)
        print(f"CodeFS image created: {path}")
        print(f"  Size: {self.size:,} bytes ({self.block_count:,} blocks)")
        print(f"  Inodes: {self.inode_counter - 1}")
        print(f"  Data area: block {self.data_area_start}")


def parse_size(s):
    s = s.strip().upper()
    if s.endswith('M'):
        return int(s[:-1]) * 1024 * 1024
    elif s.endswith('G'):
        return int(s[:-1]) * 1024 * 1024 * 1024
    elif s.endswith('K'):
        return int(s[:-1]) * 1024
    return int(s)


def main():
    parser = argparse.ArgumentParser(description='Create a CodeFS filesystem image')
    parser.add_argument('image', help='Output image file path')
    parser.add_argument('--size', default='16M', help='Image size (default: 16M)')
    parser.add_argument('--root-dir', help='Populate from this directory')
    args = parser.parse_args()

    size = parse_size(args.size)
    if size < CODEFS_BLOCK_SIZE * 64:
        print("Error: minimum size is 256K", file=sys.stderr)
        sys.exit(1)

    img = CodeFSImage(size)

    if args.root_dir:
        if not os.path.isdir(args.root_dir):
            print(f"Error: {args.root_dir} is not a directory", file=sys.stderr)
            sys.exit(1)
        root_ino = img.create_root()
        img.populate_from_dir(args.root_dir, root_ino)
        print(f"Populated from {args.root_dir}")

    img.finalize()
    img.save(args.image)


if __name__ == '__main__':
    main()
