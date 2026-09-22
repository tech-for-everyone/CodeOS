#!/usr/bin/env python3
"""Generate kernel/initramfs_files.c from userspace ELF binaries and assets."""
import sys, os, glob

sys.stdout.write('#include "fs.h"\n')
sys.stdout.write('#include "kprintf.h"\n\n')

files = []

# Process command line arguments (ELF binaries)
for path in sys.argv[1:]:
    with open(path, 'rb') as f:
        data = f.read()
    name = path.rstrip('/').split('/')[-1]
    cname = '_binary_' + name.replace('.', '_').replace('-', '_')
    sys.stdout.write(f'static const unsigned char {cname}[] = {{\n')
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        sys.stdout.write('  ' + ', '.join(f'0x{b:02x}' for b in chunk))
        sys.stdout.write(',\n')
    sys.stdout.write('};\n')
    sys.stdout.write(f'static const int {cname}_len = {len(data)};\n\n')
    files.append((name, cname))

# Include assets from icons directory (both .svg and .rgba)
# An optional keep.list in that directory (one filename per line, '#' comments)
# restricts which icons get embedded; without it every icon is embedded.
icons_dir = 'userspace/icons'
if os.path.exists(icons_dir):
    keep = None
    keep_path = os.path.join(icons_dir, 'keep.list')
    if os.path.exists(keep_path):
        keep = set()
        with open(keep_path, 'r') as kf:
            for line in kf:
                line = line.strip()
                if line and not line.startswith('#'):
                    keep.add(line)
    for icon_path in sorted(glob.glob(os.path.join(icons_dir, '*.svg')) + glob.glob(os.path.join(icons_dir, '*.rgba')) + glob.glob(os.path.join(icons_dir, '*.png'))):
        if keep is not None and os.path.basename(icon_path) not in keep:
            continue
        with open(icon_path, 'rb') as f:
            data = f.read()
        name = os.path.basename(icon_path)
        cname = '_binary_icon_' + name.replace('.', '_').replace('-', '_')
        vis = '' if cname.startswith('_binary_icon_') else 'static '
        sys.stdout.write(f'{vis}const unsigned char {cname}[] = {{\n')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            sys.stdout.write('  ' + ', '.join(f'0x{b:02x}' for b in chunk))
            sys.stdout.write(',\n')
        sys.stdout.write('};\n')
        sys.stdout.write(f'{vis}const int {cname}_len = {len(data)};\n\n')
        files.append((name, cname))

# csl games from userspace/games/*.csl -> /usr/share/games/<name>
games_dir = 'userspace/games'
if os.path.exists(games_dir):
    for gpath in sorted(glob.glob(os.path.join(games_dir, '*.csl'))):
        with open(gpath, 'rb') as f:
            data = f.read()
        name = os.path.basename(gpath)
        cname = '_binary_game_' + name.replace('.', '_').replace('-', '_')
        sys.stdout.write(f'static const unsigned char {cname}[] = {{\n')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            sys.stdout.write('  ' + ', '.join(f'0x{b:02x}' for b in chunk))
            sys.stdout.write(',\n')
        sys.stdout.write('};\n')
        sys.stdout.write(f'static const int {cname}_len = {len(data)};\n\n')
        files.append((f'/usr/share/games/{name}', cname))

# NetBeam network-share sample: a small file that ships on every system so
# AirDrop-style transfers have an obvious shareable target.
sample_text = b'Hello from NetBeam! This file was sent over the network, AirDrop-style.\n'
sys.stdout.write('static const unsigned char _asset_nbsample[] = {\n')
for i in range(0, len(sample_text), 16):
    chunk = sample_text[i:i+16]
    sys.stdout.write('  ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',\n')
sys.stdout.write('};\n')
sys.stdout.write(f'static const int _asset_nbsample_len = {len(sample_text)};\n\n')
files.append(('/NetBeam-hello.txt', '_asset_nbsample'))

sys.stdout.write('static const struct { const char *name; const unsigned char *data; int len; } initramfs_tab[] = {\n')
for name, cname in files:
    if name.startswith('/'):
        dest = name
    elif name == 'ld-codeos':
        dest = '/lib/ld-codeos.so'
    elif name == 'init':
        dest = '/init'
    elif name.endswith('.svg') or name.endswith('.rgba') or name.endswith('.png'):
        dest = f'/usr/share/icons/{name}'
    else:
        dest = f'/bin/{name}'
    sys.stdout.write(f'  {{ "{dest}", {cname}, {cname}_len }},\n')
sys.stdout.write('  { 0, 0, 0 }\n')
sys.stdout.write('};\n\n')

sys.stdout.write('void initramfs_populate(void) {\n')
sys.stdout.write('  /* create directories */\n')
sys.stdout.write('  fs_mkdir("/lib");\n')
sys.stdout.write('  fs_mkdir("/bin");\n')
sys.stdout.write('  fs_mkdir("/usr");\n')
sys.stdout.write('  fs_mkdir("/usr/share");\n')
sys.stdout.write('  fs_mkdir("/usr/share/icons");\n')
sys.stdout.write('  fs_mkdir("/usr/share/games");\n')
sys.stdout.write('  if (fs_resolve("/lib", 0) < 0) {\n')
sys.stdout.write('    kprintf("initramfs: cannot create /lib\\n"); return;\n')
sys.stdout.write('  }\n')
sys.stdout.write('  kprintf("initramfs: populating %d files\\n", (int)(sizeof(initramfs_tab)/sizeof(initramfs_tab[0]) - 1));\n')
sys.stdout.write('  for (int i = 0; initramfs_tab[i].name; i++) {\n')
sys.stdout.write('    const char *p = initramfs_tab[i].name;\n')
sys.stdout.write('    fs_mkfile(p);\n')
sys.stdout.write('    if (fs_write(p, (const char*)initramfs_tab[i].data, initramfs_tab[i].len) < 0)\n')
sys.stdout.write('      kprintf("initramfs: failed to write %s\\n", p);\n')
sys.stdout.write('  }\n')
sys.stdout.write('}\n')
