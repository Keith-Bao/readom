#!/usr/bin/env python3
"""The marker's wiring into the staging module, and the two traps in it.

The marker exists so that a jump which dies before the new kernel's printk can
be told apart from a jump that never happened (ports/koa2/recovery.md section
4). Writing it is only half the job: it has to reach DRAM, and the module has
to still load.

The first trap is the cache. A watchdog reset on this SoC does not clean the
caches, so a marker sitting in a dirty line is a marker that was never written
-- and the failure is invisible, because the code looks like it worked. So the
write-back is not optional: koa2_marker_place refuses without one.

The second trap is subtler and was hit while writing this. Cleaning by address
(__cpuc_flush_dcache_area) compiles to v7_flush_kern_dcache_area, which no
stock module imports. A module importing it cannot have its CRC checked, and
the stock loader refuses it -- so the marker would be unreachable on the
device, for a reason that has nothing to do with the marker. flush_cache_all
compiles to v7_flush_kern_cache_all, which mxc_epdc_v2_fb imports. Likewise,
get_jiffies_64()/HZ pulls in __aeabi_uldivmod, which the stock kernel does not
export at all.

So: every symbol the marker path uses must be one the stock modules carry a CRC
for, and the marker must stay separate from the jump, which is not built into
this module and is not enabled by writing a marker.
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STOCK = ROOT / 'ports/koa2/materials/2026-11-21/device-evidence/stock-modules'
CRCS = ROOT / 'ports/koa2/module-crcs.py'
STAGE = (ROOT / 'boot/koa2/stage.c').read_text(encoding='utf-8')
# The forbidden names are named in the comments that explain why they are
# forbidden, so the refusals have to read the code and not the prose.
STAGE_CODE = re.sub(r'/\*.*?\*/', ' ', STAGE, flags=re.S)
MARKER_C = (ROOT / 'boot/koa2/marker.c').read_text(encoding='utf-8')
MARKER_H = (ROOT / 'boot/koa2/marker.h').read_text(encoding='utf-8')
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


# The symbols the marker path reaches for, and what each must be true of.
# flush_cache_all and outer_clean_range are what stage.c calls; the names on the
# right are what they compile to and what the loader checks.
REQUIRED = {'flush_cache_all': 'v7_flush_kern_cache_all', 'outer_clean_range': 'outer_cache'}
# Cleaning by address, and 64-bit division: both make the module unloadable.
REFUSED = ['__cpuc_flush_dcache_area', 'v7_flush_kern_dcache_area', 'get_jiffies_64']

stock = {}
if STOCK.is_dir():
    out = subprocess.run([sys.executable, str(CRCS), 'read'] +
                         [str(p) for p in sorted(STOCK.rglob('*.ko'))],
                         capture_output=True, text=True)
    if out.returncode == 0:
        for module in json.loads(out.stdout):
            stock.update(module['versions'])

marker_path = STAGE_CODE[STAGE_CODE.index('static void marker_write_back'):
                         STAGE_CODE.index('static int seal_fail')]
for called, symbol in REQUIRED.items():
    check(called in marker_path, '%s is not called in the marker path' % called)
    if stock:
        check(symbol in stock, '%s has no stock CRC, so the module would be refused' % symbol)
for name in REFUSED:
    check(name not in STAGE_CODE, '%s is back in stage.c; the module would not load' % name)

# The write-back is a parameter and refusing without it is the point: a marker
# that never leaves the cache reads as success and is not.
check('write_back' in MARKER_H, 'marker.h no longer takes a write-back')
check(re.search(r'!write_back\)\s*return 0', MARKER_C) is not None,
      'koa2_marker_place no longer refuses without a write-back')

# It lands at the end of the live half, which is the half the next boot saves,
# and it clears its slot so the end of the marker is unambiguous.
check('target->bytes - KOA2_MARKER_SLOT' in MARKER_C, 'the marker no longer lands at the end of the half')
check('0x9be00000u' in STAGE, 'stage.c no longer names the Lab126 printk buffer')

# Marking is not a jump and must not become the way one appears.
check('execute_available=0' in STAGE, 'the module no longer reports execute_available=0')
for name in ['koa2_jump_start', 'jump.S', 'KOA2_JUMP_BUILD']:
    check(name not in STAGE, 'stage.c now reaches for the jump (%s)' % name)
# The marker needs a sealed plan: the addresses it records are that plan's.
check('K2S_SEALED || !s->plan_valid' in STAGE, 'marking no longer requires a sealed, valid plan')

if failures:
    for f in failures:
        print('FAIL:', f)
    raise SystemExit(1)
print('marker wiring: the write-back is required, every symbol it needs has a stock CRC, '
      'the by-address flush and 64-bit division that make the module unloadable are absent, '
      'and marking still needs a sealed plan and grants no jump')
