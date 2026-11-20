#!/usr/bin/env python3
"""ports/koa2/module-crcs.py, on modules made here byte by byte.

The checker decides whether the handoff module is offered to the stock KOA2
kernel at all, and the stock modules it reads come off the device. So it is
held to the loader's rules (kernel/module.c, Linux 4.1): the vermagic compared
after the first space, module_layout and every import matched by CRC, and an
import with no stock CRC counted as a failure, not a pass. It must also refuse
input it cannot read rather than report on part of it.
"""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / 'ports/koa2/module-crcs.py'
STOCK_MAGIC = '4.1.15-lab126 SMP preempt mod_unload modversions ARMv7 p2v8 '
SMOKE_MAGIC = '4.1.15-lab126-koa2-headless-smoke' + STOCK_MAGIC[STOCK_MAGIC.index(' '):]
failures = []


def check(ok, message):
    if not ok:
        failures.append(message)


def ko(path, vermagic, versions, entry=64, machine=40):
    """An ARM ELF32 relocatable with .modinfo and __versions, nothing else."""
    modinfo = b'license=GPL' + bytes(1) + ('vermagic=' + vermagic).encode() + bytes(1)
    raw = b''.join(struct.pack('<I', crc) + name.encode().ljust(entry - 4, bytes(1)) for name, crc in versions)
    shstr = bytes(1) + b'.modinfo' + bytes(1) + b'__versions' + bytes(1) + b'.shstrtab' + bytes(1)
    body = modinfo + raw + shstr
    shoff = 52 + len(body)
    shoff += (-shoff) % 4
    header = bytearray(52)
    header[:6] = bytes([0x7f]) + b'ELF' + bytes([1, 1])
    struct.pack_into('<HHI', header, 16, 1, machine, 1)
    struct.pack_into('<I', header, 32, shoff)
    struct.pack_into('<HHHHHH', header, 40, 52, 0, 0, 40, 4, 3)
    data = bytearray(header) + body
    data += bytes(shoff - len(data))

    def sh(name, offset, size):
        return struct.pack('<IIIIIIIIII', name, 1, 0, 0, offset, size, 0, 0, 1, 0)
    data += bytes(40)
    data += sh(1, 52, len(modinfo))
    data += sh(10, 52 + len(modinfo), len(raw))
    data += sh(21, 52 + len(modinfo) + len(raw), len(shstr))
    path.write_bytes(bytes(data))
    return path


def run(*args):
    p = subprocess.run([sys.executable, '-I', str(TOOL), *map(str, args)], capture_output=True, text=True)
    report = json.loads(p.stdout) if p.stdout.strip().startswith(('{', '[')) else None
    return p.returncode, report, p.stderr


def loader_rules(t, stock):
    good = ko(t / 'good.ko', SMOKE_MAGIC, [('module_layout', 0x11), ('kfree', 0x22), ('misc_register', 0x44)])
    code, r, _ = run('read', good)
    check(code == 0 and r[0]['versions'] == {'kfree': '0x00000022', 'misc_register': '0x00000044',
                                             'module_layout': '0x00000011'}, f'read: {code} {r}')
    code, r, err = run('check', '--stock', stock, good)
    check(code == 0 and r and r['passed'] and r['matched'] == 3 and r['stock_modules'] == 2 and
          r['module_layout'] == 'match' and r['vermagic_tail_equal'], f'release differs only: {code} {r} {err}')
    # One CRC wrong: the loader says "disagrees about version of symbol".
    bad = ko(t / 'bad.ko', SMOKE_MAGIC, [('module_layout', 0x11), ('kfree', 0x23)])
    code, r, _ = run('check', '--stock', stock, bad)
    check(code == 1 and not r['passed'] and r['differ'] == [{'symbol': 'kfree', 'module': '0x00000023',
          'stock': '0x00000022'}], f'differing CRC: {code} {r}')
    # An import no stock module records cannot be checked, so it is not a pass.
    unknown = ko(t / 'unknown.ko', SMOKE_MAGIC, [('module_layout', 0x11), ('__vmalloc', 0x55)])
    code, r, _ = run('check', '--stock', stock, unknown)
    check(code == 1 and r['unknown'] == ['__vmalloc'], f'unknown import: {code} {r}')
    layout = ko(t / 'layout.ko', SMOKE_MAGIC, [('module_layout', 0x12)])
    code, r, _ = run('check', '--stock', stock, layout)
    check(code == 1 and r['module_layout'] == 'differs', f'module_layout: {code} {r}')
    # The part after the release has to match exactly (here: no preempt).
    other = ko(t / 'other.ko', '4.1.15-lab126 SMP mod_unload modversions ARMv7 p2v8 ', [('module_layout', 0x11)])
    code, r, _ = run('check', '--stock', stock, other)
    check(code == 1 and not r['vermagic_tail_equal'], f'vermagic tail: {code} {r}')
    # No __versions at all would need a forced load; it never passes.
    empty = ko(t / 'empty.ko', SMOKE_MAGIC, [])
    code, r, _ = run('check', '--stock', stock, empty)
    check(code == 1 and not r['passed'], f'no versions: {code} {r}')
    # --symvers says how the build tree agrees with stock on symbols both know.
    symvers = t / 'Module.symvers'
    symvers.write_text('0x00000022\tkfree\tvmlinux\tEXPORT_SYMBOL\n'
                       '0x00000099\tvzalloc\tvmlinux\tEXPORT_SYMBOL\n'
                       '0x00000077\tonly_here\tvmlinux\tEXPORT_SYMBOL\n')
    code, r, _ = run('check', '--stock', stock, '--symvers', symvers, good)
    check(r and r['tree_vs_stock'] == {'symvers': str(symvers), 'common': 2, 'equal': 1, 'differ': ['vzalloc']},
          f'tree vs stock: {r and r.get("tree_vs_stock")}')
    return good


def refusals(t, good):
    # Stock modules that disagree, mixed vermagics, nothing found.
    split = t / 'split'
    split.mkdir()
    ko(split / 'a.ko', STOCK_MAGIC, [('kfree', 0x22)])
    ko(split / 'b.ko', STOCK_MAGIC, [('kfree', 0x24)])
    code, r, err = run('check', '--stock', split, good)
    check(code != 0 and r is None and 'disagree' in err, f'stock disagreement: {code} {err}')
    mixed = t / 'mixed'
    mixed.mkdir()
    ko(mixed / 'a.ko', STOCK_MAGIC, [('kfree', 0x22)])
    ko(mixed / 'b.ko', SMOKE_MAGIC, [('kfree', 0x22)])
    code, r, err = run('check', '--stock', mixed, good)
    check(code != 0 and r is None and 'vermagics' in err, f'mixed vermagic: {code} {err}')
    nothing = t / 'nothing'
    nothing.mkdir()
    code, r, err = run('check', '--stock', nothing, good)
    check(code != 0 and r is None and 'no stock modules' in err, f'empty stock: {code} {err}')
    # A torn __versions, a non-ARM file, a truncated file.
    torn = ko(t / 'torn.ko', SMOKE_MAGIC, [('kfree', 0x22)], entry=60)
    code, r, err = run('read', torn)
    check(code != 0 and r is None and 'multiple' in err, f'torn __versions: {code} {err}')
    x86 = ko(t / 'x86.ko', SMOKE_MAGIC, [('kfree', 0x22)], machine=3)
    code, r, err = run('read', x86)
    check(code != 0 and r is None and 'ARM' in err, f'not ARM: {code} {err}')
    short = t / 'short.ko'
    short.write_bytes(bytes([0x7f]) + b'ELF')
    code, r, err = run('read', short)
    check(code != 0 and r is None, f'truncated: {code} {err}')


with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    stock = t / 'stock'
    (stock / 'kernel').mkdir(parents=True)
    ko(stock / 'kernel/a.ko', STOCK_MAGIC, [('module_layout', 0x11), ('kfree', 0x22), ('vzalloc', 0x33)])
    ko(stock / 'b.ko', STOCK_MAGIC, [('module_layout', 0x11), ('misc_register', 0x44)])
    refusals(t, loader_rules(t, stock))

for failure in failures:
    print('FAIL', failure)
if failures:
    sys.exit(1)
print('module-crcs: loader rules (vermagic tail, module_layout, CRC match, unknown imports) and 6 refusals hold')
