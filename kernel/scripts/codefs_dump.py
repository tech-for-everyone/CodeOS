#!/usr/bin/env python3
"""codefs_dump.py — Inspect and dump a CodeFS filesystem image.

Reads and displays the superblock, inode table, block/inode bitmaps,
directory trees, and file contents from a CodeFS image.

Usage:
    python3 scripts/codefs_dump.py disk.img              # dump all
    python3 scripts/codefs_dump.py disk.img --superblock  # superblock only
    python3 scripts/codefs_dump.py disk.img --tree        # directory tree
    python3 scripts/codefs_dump.py disk.img --inodes      # all inodes
    python3 scripts/codefs_dump.py disk.img --bitmap      # block/inode bitmaps
    python3 scripts/codefs_dump.py disk.img --file /path  # dump file contents
    python3 scripts/codefs_dump.py disk.img --verify      # verify CRC checksums
"""

import argparse
import os
import struct
import sys

CODEFS_BLOCK_SIZE = 4096
CODEFS_INODE_SIZE = 256
CODEFS_INLINE_MAX = 60
CODEFS_MAX_EXTENTS = 4
CODEFS_MAGIC = 0x434F4445

FT_NAMES = {0: '???', 1: 'REG', 2: 'DIR', 3: 'LNK'}

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


# ── Image reader ─────────────────────────────────────────────

class CodeFSDump:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        self.size = len(self.data)
        self.block_count = self.size // CODEFS_BLOCK_SIZE
        self.sb = None
        self.inodes = {}
        self.read_superblock()

    def read_block(self, n):
        off = n * CODEFS_BLOCK_SIZE
        return self.data[off:off + CODEFS_BLOCK_SIZE]

    def read_superblock(self):
        raw = self.read_block(1)
        # Unpack first 23 fields (88 bytes)
        fmt = '<IIIIIIIIIIIIIIIIIIIIIII'
        fields = struct.unpack_from(fmt, raw, 0)
        names = [
            'magic', 'version', 'flags', 'block_size', 'blocks_count',
            'free_blocks', 'inodes_count', 'free_inodes', 'root_inode',
            'journal_inode', 'journal_start', 'journal_len',
            'inode_table_start', 'data_area_start', 'inode_count',
            'total_inodes', 'mount_count', 'max_mounts', 'state',
            'created_time', 'last_mount', 'last_write', 'crc'
        ]
        self.sb = dict(zip(names, fields))
        self.sb['volume_name'] = raw[88:104].rstrip(b'\x00').decode('utf-8', errors='replace')

    def read_inode(self, ino_num):
        if ino_num in self.inodes:
            return self.inodes[ino_num]
        idx = ino_num - 1
        byte_off = idx * CODEFS_INODE_SIZE
        blk = self.sb['inode_table_start'] + (byte_off // CODEFS_BLOCK_SIZE)
        off_in_blk = byte_off % CODEFS_BLOCK_SIZE
        raw = self.read_block(blk)
        chunk = raw[off_in_blk:off_in_blk + CODEFS_INODE_SIZE]

        fields = struct.unpack_from('<HHHIIIIIII', chunk, 0)
        ino = {
            'mode': fields[0],
            'uid': fields[1],
            'gid': fields[2],
            'size': fields[3],
            'atime': fields[4],
            'ctime': fields[5],
            'mtime': fields[6],
            'links': fields[7],
            'flags': fields[8],
            'inline_len': fields[9],
        }

        # Inline data (offset 34)
        ino['inline_data'] = chunk[34:34 + CODEFS_INLINE_MAX]

        # Extents (offset 34 + 60 = 94)
        extents = []
        ext_off = 34 + CODEFS_INLINE_MAX
        for i in range(CODEFS_MAX_EXTENTS):
            e = struct.unpack_from('<IIII', chunk, ext_off + i * 16)
            if e[0] != 0 or e[2] != 0:
                extents.append({'file_blk': e[0], 'length': e[1], 'disk_start': e[2]})
        ino['extents'] = extents
        ino['extent_count'] = struct.unpack_from('<I', chunk, ext_off + CODEFS_MAX_EXTENTS * 16)[0]

        # CRC (offset 170)
        ino['crc'] = struct.unpack_from('<I', chunk, 170)[0]

        self.inodes[ino_num] = ino
        return ino

    def read_file_data(self, ino):
        if ino['size'] <= CODEFS_INLINE_MAX and ino['inline_len'] > 0:
            return ino['inline_data'][:ino['inline_len']]
        data = bytearray()
        for e in ino['extents']:
            for b in range(e['length']):
                blk = e['disk_start'] + b
                data.extend(self.read_block(blk))
        return bytes(data[:ino['size']])

    def list_dir(self, ino_num, indent=0):
        ino = self.read_inode(ino_num)
        if not (ino['mode'] & 0o40000):
            return
        blocks = (ino['size'] + CODEFS_BLOCK_SIZE - 1) // CODEFS_BLOCK_SIZE
        for e in ino['extents']:
            for b in range(e['length']):
                blk = e['disk_start'] + b
                raw = self.read_block(blk)
                off = 0
                while off < CODEFS_BLOCK_SIZE:
                    if off + 8 > CODEFS_BLOCK_SIZE:
                        break
                    child_ino = struct.unpack_from('<I', raw, off)[0]
                    rec_len = struct.unpack_from('<H', raw, off + 4)[0]
                    name_len = struct.unpack_from('B', raw, off + 6)[0]
                    ft = struct.unpack_from('B', raw, off + 7)[0]
                    if child_ino == 0 and rec_len == 0:
                        break
                    if rec_len == 0:
                        break
                    name = raw[off + 8:off + 8 + name_len].decode('utf-8', errors='replace')
                    child = self.read_inode(child_ino)
                    marker = '/' if child['mode'] & 0o40000 else ''
                    print(f"{'  ' * indent}{name}{marker}  (ino={child_ino}, {FT_NAMES.get(ft, '???')}, {child['size']} bytes)")
                    if child['mode'] & 0o40000 and name not in ('.', '..'):
                        self.list_dir(child_ino, indent + 1)
                    off += rec_len

    def dump_all(self):
        self.dump_superblock()
        self.dump_bitmaps()
        self.dump_inodes()
        self.dump_tree()

    def dump_superblock(self):
        sb = self.sb
        print("=" * 64)
        print("CodeFS Superblock")
        print("=" * 64)
        print(f"  Magic:          0x{sb['magic']:08X} {'OK' if sb['magic'] == CODEFS_MAGIC else 'BAD'}")
        print(f"  Version:        {sb['version']}")
        print(f"  Flags:          0x{sb['flags']:04X}")
        print(f"  Block size:     {sb['block_size']}")
        print(f"  Blocks:         {sb['blocks_count']}")
        print(f"  Free blocks:    {sb['free_blocks']}")
        print(f"  Inodes:         {sb['inodes_count']}")
        print(f"  Free inodes:    {sb['free_inodes']}")
        print(f"  Root inode:     {sb['root_inode']}")
        print(f"  Journal start:  {sb['journal_start']}")
        print(f"  Journal len:    {sb['journal_len']}")
        print(f"  Inode table:    block {sb['inode_table_start']}")
        print(f"  Data area:      block {sb['data_area_start']}")
        print(f"  Mount count:    {sb['mount_count']}/{sb['max_mounts']}")
        state_names = {1: 'CLEAN', 2: 'DIRTY', 4: 'ERROR'}
        print(f"  State:          {state_names.get(sb['flags'] & 7, 'UNKNOWN')}")
        print(f"  Created:        {sb['created_time']}")
        print(f"  CRC:            0x{sb['crc']:08X}")

    def dump_bitmaps(self):
        print("\n" + "=" * 64)
        print("Block Bitmap (first 256 bits)")
        print("=" * 64)
        raw = self.read_block(2)
        for i in range(32):
            byte = raw[i]
            bits = ''
            for b in range(8):
                bits += '#' if byte & (1 << b) else '.'
            print(f"  {i*8:4d}: {bits}")

        print("\n" + "=" * 64)
        print("Inode Bitmap (first 256 bits)")
        print("=" * 64)
        raw = self.read_block(3)
        for i in range(32):
            byte = raw[i]
            bits = ''
            for b in range(8):
                bits += '#' if byte & (1 << b) else '.'
            print(f"  {i*8:4d}: {bits}")

    def dump_inodes(self):
        print("\n" + "=" * 64)
        print("Inode Table")
        print("=" * 64)
        for ino_num in range(1, self.sb['inode_count'] + 1):
            ino = self.read_inode(ino_num)
            mode_str = oct(ino['mode'])
            extent_str = f"{ino['extent_count']} extents" if ino['extent_count'] else "inline"
            print(f"  Inode {ino_num:4d}: mode={mode_str} size={ino['size']:6d} links={ino['links']} {extent_str}")

    def dump_tree(self):
        print("\n" + "=" * 64)
        print("Directory Tree")
        print("=" * 64)
        self.list_dir(self.sb['root_inode'])

    def dump_file(self, path):
        parts = [p for p in path.strip('/').split('/') if p]
        cur_ino = self.sb['root_inode']
        for part in parts:
            ino = self.read_inode(cur_ino)
            if not (ino['mode'] & 0o40000):
                print(f"Error: not a directory at inode {cur_ino}")
                return
            found = False
            for e in ino['extents']:
                for b in range(e['length']):
                    raw = self.read_block(e['disk_start'] + b)
                    off = 0
                    while off < CODEFS_BLOCK_SIZE:
                        child_ino = struct.unpack_from('<I', raw, off)[0]
                        rec_len = struct.unpack_from('<H', raw, off + 4)[0]
                        name_len = struct.unpack_from('B', raw, off + 6)[0]
                        if child_ino == 0 and rec_len == 0:
                            break
                        if rec_len == 0:
                            break
                        name = raw[off + 8:off + 8 + name_len].decode('utf-8', errors='replace')
                        if name == part:
                            cur_ino = child_ino
                            found = True
                            break
                        off += rec_len
                    if found:
                        break
                if found:
                    break
            if not found:
                print(f"Error: '{part}' not found")
                return

        ino = self.read_inode(cur_ino)
        if ino['mode'] & 0o40000:
            print(f"'{path}' is a directory")
            return
        data = self.read_file_data(ino)
        try:
            print(data.decode('utf-8'))
        except UnicodeDecodeError:
            sys.stdout.buffer.write(data)

    def verify(self):
        print("=" * 64)
        print("CRC Verification")
        print("=" * 64)
        # Superblock
        raw = self.read_block(1)
        stored = struct.unpack_from('<I', raw, 444)[0]
        computed = crc32(raw[:444])
        ok = stored == computed
        print(f"  Superblock:  stored=0x{stored:08X} computed=0x{computed:08X} {'OK' if ok else 'MISMATCH'}")

        # Inodes
        bad = 0
        for ino_num in range(2, self.sb['inode_count'] + 1):
            ino = self.read_inode(ino_num)
            # Reconstruct raw
            raw_inode = bytearray()
            raw_inode += struct.pack('<HHHIIIIIII',
                ino['mode'], ino['uid'], ino['gid'], ino['size'],
                ino['atime'], ino['ctime'], ino['mtime'], ino['links'],
                ino['flags'], ino['inline_len'])
            raw_inode += ino['inline_data'][:CODEFS_INLINE_MAX]
            for e in ino['extents'][:CODEFS_MAX_EXTENTS]:
                raw_inode += struct.pack('<IIII', e['file_blk'], e['length'], e['disk_start'], 0)
            for _ in range(CODEFS_MAX_EXTENTS - len(ino['extents'])):
                raw_inode += b'\x00' * 16
            raw_inode += struct.pack('<I', ino['extent_count'])
            raw_inode += b'\x00' * 8
            raw_inode += b'\x00\x00\x00\x00'  # CRC placeholder
            # Pad to full inode size
            raw_inode += b'\x00' * (CODEFS_INODE_SIZE - len(raw_inode))
            computed = crc32(bytes(raw_inode))
            stored = ino['crc']
            if stored != computed:
                print(f"  Inode {ino_num}:  stored=0x{stored:08X} computed=0x{computed:08X} MISMATCH")
                bad += 1
        if bad == 0:
            print(f"  Inodes:      all {self.sb['inode_count']} CRCs OK")
        else:
            print(f"  Inodes:      {bad} mismatches")


def main():
    parser = argparse.ArgumentParser(description='Inspect a CodeFS image')
    parser.add_argument('image', help='CodeFS image file')
    parser.add_argument('--superblock', action='store_true', help='Superblock only')
    parser.add_argument('--tree', action='store_true', help='Directory tree')
    parser.add_argument('--inodes', action='store_true', help='Inode table')
    parser.add_argument('--bitmap', action='store_true', help='Bitmaps')
    parser.add_argument('--file', help='Dump file contents')
    parser.add_argument('--verify', action='store_true', help='Verify CRC checksums')
    args = parser.parse_args()

    if not os.path.exists(args.image):
        print(f"Error: {args.image} not found", file=sys.stderr)
        sys.exit(1)

    dump = CodeFSDump(args.image)

    if args.superblock:
        dump.dump_superblock()
    elif args.tree:
        dump.dump_tree()
    elif args.inodes:
        dump.dump_inodes()
    elif args.bitmap:
        dump.dump_bitmaps()
    elif args.file:
        dump.dump_file(args.file)
    elif args.verify:
        dump.verify()
    else:
        dump.dump_all()


if __name__ == '__main__':
    main()
