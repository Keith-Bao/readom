#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Whether a module built here would pass the stock KOA2 kernel's version checks.

Linux 4.1 with CONFIG_MODVERSIONS (kernel/module.c) loads a module only if
  * its vermagic equals the kernel's after the first space (same_magic skips
    the release string when the module carries CRCs), and
  * for module_layout and every symbol it imports, the CRC in its __versions
    section equals the one the kernel exports (check_version); a symbol it
    imports with no __versions entry is refused too.
The stock kernel's CRCs are not readable on the device (they are absolute
symbols, which /proc/kallsyms leaves out), but every stock module records, in
its own __versions, the CRC of each symbol it was linked against. The union of
those is what this compares against.

  module-crcs.py read MODULE.ko...            print vermagic and __versions as JSON
  module-crcs.py check --stock DIR|KO [--stock ...] [--symvers Module.symvers] MODULE.ko

check exits 0 only if the vermagic tails are equal and every entry in
MODULE.ko's __versions has a stock CRC and agrees with it. --symvers also
reports how the tree MODULE.ko was built against agrees with stock on every
symbol both know, which says whether rebuilding against it can work at all.
It reads files only.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

ENTRY = 64          # struct modversion_info on 32-bit: unsigned long crc; char name[64 - 4]


def fail(message):
    raise SystemExit('module-crcs: ' + message)


def sections(path):
    data = path.read_bytes()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        fail(f'{path}: not a little-endian ELF32 file')
    if struct.unpack_from('<HH', data, 16) != (1, 40):
        fail(f'{path}: not an ARM relocatable object')
    shoff, = struct.unpack_from('<I', data, 32)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 46)
    if shentsize != 40 or not shnum or shstrndx >= shnum or shoff + shnum * 40 > len(data):
        fail(f'{path}: bad section header table')
    headers = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * 40) for i in range(shnum)]
    names = headers[shstrndx]
    out = {}
    for h in headers:
        start = names[4] + h[0]
        if start >= len(data):
            fail(f'{path}: section name outside the file')
        name = data[start:data.index(b'\0', start)].decode('ascii', 'replace')
        if h[1] == 8:                          # SHT_NOBITS
            continue
        if h[4] + h[5] > len(data):
            fail(f'{path}: section {name} outside the file')
        out[name] = data[h[4]:h[4] + h[5]]
    return out


def read(path):
    s = sections(path)
    vermagic = None
    for field in s.get('.modinfo', b'').split(b'\0'):
        if field.startswith(b'vermagic='):
            if vermagic is not None:
                fail(f'{path}: two vermagic fields')
            vermagic = field[9:].decode('ascii', 'replace')
    if vermagic is None:
        fail(f'{path}: no vermagic')
    raw = s.get('__versions', b'')
    if len(raw) % ENTRY:
        fail(f'{path}: __versions is {len(raw)} bytes, not a multiple of {ENTRY}')
    versions = {}
    for i in range(0, len(raw), ENTRY):
        crc, = struct.unpack_from('<I', raw, i)
        name = raw[i + 4:i + ENTRY]
        if b'\0' not in name:
            fail(f'{path}: unterminated name in __versions')
        name = name[:name.index(b'\0')].decode('ascii', 'replace')
        if name in versions and versions[name] != crc:
            fail(f'{path}: {name} listed twice with different CRCs')
        versions[name] = crc
    return {'module': str(path), 'vermagic': vermagic, 'versions': versions}


def tail(vermagic):
    return vermagic[vermagic.index(' '):] if ' ' in vermagic else ''


def stock_table(paths):
    """symbol -> CRC from every stock module; refuses if two stock modules disagree."""
    table, source, vermagics, count = {}, {}, set(), 0
    for p in paths:
        files = sorted(p.rglob('*.ko')) if p.is_dir() else [p]
        for f in files:
            info = read(f)
            count += 1
            vermagics.add(info['vermagic'])
            for name, crc in info['versions'].items():
                if name in table and table[name] != crc:
                    fail(f'stock modules disagree on {name}: {source[name]} and {f}')
                table[name], source[name] = crc, str(f)
    if not count:
        fail('no stock modules found')
    if len(vermagics) != 1:
        fail(f'stock modules carry {len(vermagics)} vermagics: {sorted(vermagics)}')
    return table, vermagics.pop(), count


def symvers(path):
    out = {}
    for line in path.read_text().splitlines():
        fields = line.split('\t')
        if len(fields) >= 2:
            out[fields[1]] = int(fields[0], 16)
    return out


def check(a):
    table, stock_magic, count = stock_table(a.stock)
    mod = read(a.module)
    matched, differ, unknown = [], [], []
    for name, crc in sorted(mod['versions'].items()):
        if name not in table:
            unknown.append(name)
        elif table[name] == crc:
            matched.append(name)
        else:
            differ.append({'symbol': name, 'module': f'{crc:#010x}', 'stock': f'{table[name]:#010x}'})
    magic_ok = tail(mod['vermagic']) == tail(stock_magic)
    report = {
        'module': mod['module'], 'module_vermagic': mod['vermagic'],
        'stock_vermagic': stock_magic, 'stock_modules': count, 'stock_symbols': len(table),
        'vermagic_tail_equal': magic_ok,
        'module_layout': 'match' if 'module_layout' in matched else
                         'differs' if any(d['symbol'] == 'module_layout' for d in differ) else 'unknown',
        'imports': len(mod['versions']), 'matched': len(matched),
        'differ': differ, 'unknown': unknown,
    }
    if a.symvers:
        tree = symvers(a.symvers)
        both = sorted(set(tree) & set(table))
        report['tree_vs_stock'] = {
            'symvers': str(a.symvers), 'common': len(both),
            'equal': sum(tree[s] == table[s] for s in both),
            'differ': [s for s in both if tree[s] != table[s]],
        }
    report['passed'] = magic_ok and not differ and not unknown and bool(mod['versions'])
    report['scope'] = ('Version checks only. A pass says the stock loader would accept the CRCs; '
                       'it does not say the module is correct, safe, or has been loaded.')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = p.add_subparsers(dest='command', required=True)
    r = sub.add_parser('read')
    r.add_argument('modules', nargs='+', type=Path)
    c = sub.add_parser('check')
    c.add_argument('--stock', action='append', type=Path, required=True, help='stock module or directory; repeatable')
    c.add_argument('--symvers', type=Path)
    c.add_argument('module', type=Path)
    a = p.parse_args()
    if a.command == 'read':
        out = []
        for m in a.modules:
            info = read(m)
            info['versions'] = {k: f'{v:#010x}' for k, v in sorted(info['versions'].items())}
            out.append(info)
        print(json.dumps(out, indent=2))
        return 0
    return check(a)


if __name__ == '__main__':
    sys.exit(main())
