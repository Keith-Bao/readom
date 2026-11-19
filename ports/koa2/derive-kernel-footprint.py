#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Physical memory a KOA2 zImage writes while it decompresses and starts.

Reads one kernel build output directory and prints, as JSON, the numbers the
handoff plan (boot/koa2/plan.h) needs: where the decompressor puts the kernel,
where the kernel ends including BSS, and how many bytes past its load address
a zImage uses while running (image, BSS, stack, 64 KiB malloc).

This is the KOA2 counterpart of boot/handoff/derive_kernel_footprint.py, which
assumes gzip and a fixed zreladdr. KOA2 uses CONFIG_KERNEL_LZO and
CONFIG_AUTO_ZRELADDR (arch/arm/boot/compressed/head.S, Linux 4.1.15):
  r4 = (pc & 0xf8000000) + TEXT_OFFSET
so the kernel lands at 0x80008000 only while the zImage runs below 0x88000000,
and the decompressor writes its page directory at r4 - 16 KiB. Every compressor
appends the inflated size as 4 little-endian bytes ending at input_data_end, so
the size is read from the zImage itself and checked against Image.

It refuses anything it does not recognise instead of guessing.
"""
import argparse
import json
import struct
from pathlib import Path

PAGE = 0x1000
MALLOC = 0x10000          # head.S: "malloc space is above the relocated stack (64k max)"
PAGE_DIR = 0x4000         # head.S cache_on / kernel swapper_pg_dir: r4 - 16 KiB
WINDOW = 0x08000000       # AUTO_ZRELADDR masks pc with 0xf8000000


def fail(message):
    raise SystemExit('derive-kernel-footprint: ' + message)


def elf32(path):
    data = path.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        fail(f'{path}: not a little-endian ELF32 file')
    if struct.unpack_from('<H', data, 18)[0] != 40:
        fail(f'{path}: not an ARM ELF file')
    shoff, = struct.unpack_from('<I', data, 32)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 46)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]
    names = sections[shstrndx]

    def name(offset, table):
        start = table[4] + offset
        return data[start:data.index(b'\0', start)].decode()

    by_name = {name(s[0], names): s for s in sections}
    symbols = {}
    for s in sections:
        if s[1] != 2:      # SHT_SYMTAB
            continue
        strtab = sections[s[6]]
        for i in range(s[5] // 16):
            st_name, value, _size, _info, _other, _shndx = struct.unpack_from('<IIIBBH', data, s[4] + i * 16)
            if st_name:
                symbols.setdefault(name(st_name, strtab), value)
    return by_name, symbols


def config(path):
    values = {}
    for line in path.read_text().splitlines():
        if line.startswith('CONFIG_') and '=' in line:
            key, value = line.split('=', 1)
            values[key] = value.strip('"')
    return values


def need(symbols, key, path):
    if key not in symbols:
        fail(f'{path}: no symbol {key}')
    return symbols[key]


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('output', type=Path, help='kernel build output directory')
    a = p.parse_args()
    o = a.output
    cfg = config(o / '.config')
    for key, want in (('CONFIG_AUTO_ZRELADDR', 'y'), ('CONFIG_KERNEL_LZO', 'y'),
                      ('CONFIG_PAGE_OFFSET', '0x80000000'), ('CONFIG_VMSPLIT_2G', 'y')):
        if cfg.get(key) != want:
            fail(f'{key} is {cfg.get(key)!r}, this derivation assumes {want!r}')
    for key in ('CONFIG_ZBOOT_ROM', 'CONFIG_ARM_APPENDED_DTB', 'CONFIG_XIP_KERNEL', 'CONFIG_ARM_LPAE'):
        if cfg.get(key) == 'y':
            fail(f'{key} is set; head.S takes a path this derivation does not model')
    page_offset = phys_offset = 0x80000000   # measured RAM starts at 0x80000000; lowmem is identity mapped
    text_offset = 0x8000                     # arch/arm/Makefile textofs-y; checked against stext below
    zreladdr = phys_offset + text_offset

    vmlinux = o / 'vmlinux'
    _, ksym = elf32(vmlinux)
    stext, kend = need(ksym, 'stext', vmlinux), need(ksym, '_end', vmlinux)
    if stext != page_offset + text_offset or need(ksym, 'swapper_pg_dir', vmlinux) != stext - PAGE_DIR:
        fail('kernel does not start at PAGE_OFFSET + 0x8000 with swapper_pg_dir 16 KiB below')
    kernel_end = kend - page_offset + phys_offset

    compressed = o / 'arch/arm/boot/compressed/vmlinux'
    sections, zsym = elf32(compressed)
    for key in ('_text', 'start', 'restart', 'wont_overwrite', 'input_data_end', '_edata', '_end'):
        need(zsym, key, compressed)
    if zsym['_text'] != 0 or zsym['start'] != 0:
        fail('compressed vmlinux is not linked at 0')
    stack = sections.get('.stack')
    if not stack or stack[1] != 8 or stack[3] < zsym['_end']:   # SHT_NOBITS after _end
        fail('compressed vmlinux has no .stack section after _end')
    runtime = stack[3] + stack[5] + MALLOC

    zimage = (o / 'arch/arm/boot/zImage').read_bytes()
    image_size = (o / 'arch/arm/boot/Image').stat().st_size
    if len(zimage) != zsym['_edata']:
        fail(f'zImage is {len(zimage)} bytes, compressed vmlinux _edata is {zsym["_edata"]}')
    inflated, = struct.unpack_from('<I', zimage, zsym['input_data_end'] - 4)
    if inflated != image_size:
        fail(f'inflated size {inflated} in zImage differs from Image {image_size}')
    if kernel_end < zreladdr + inflated:
        fail('kernel _end lies below the end of the inflated image')

    page_up = lambda v: (v + PAGE - 1) & ~(PAGE - 1)
    # head.S skips relocating itself iff r4 + inflated <= load + wont_overwrite.
    no_relocation_min_load = zreladdr + inflated - zsym['wont_overwrite']
    print(json.dumps({
        'zreladdr': hex(zreladdr),
        'page_directory': [hex(zreladdr - PAGE_DIR), hex(zreladdr)],
        'inflated_size': inflated,
        'kernel_end': hex(kernel_end),
        'footprint': [hex(zreladdr - PAGE_DIR), hex(page_up(kernel_end))],
        'zimage_size': len(zimage),
        'zimage_runtime': runtime,
        'zimage_load_window': [hex(phys_offset), hex(phys_offset + WINDOW)],
        'no_relocation_min_load': hex(no_relocation_min_load),
        'scope': 'Decompression and early-boot writes of this build only; says nothing about '
                 'what the running kernel allocates later or whether it boots.',
    }, indent=2))


if __name__ == '__main__':
    main()
