#!/usr/bin/env python3
"""The stock KOA2 kernel's symbol CRCs, as the device's own modules record them.

On 2026-11-21 the staging module loaded into the stock kernel, so its 31
imports and module_layout carry the CRCs the stock kernel exports. The 63 stock
modules the device copied back are in the repository, which makes that a fact
this test can hold rather than a result to remember.

It also records what does NOT match, because the reason matters:

  * nine framebuffer symbols differ, because the build tree has the EINK panel
    off (its source is missing, ports/koa2/epdc-source-gap.md). The module
    imports none of them;
  * three scheduler symbols differed until CONFIG_FALCON_WRAPPER was turned
    back on in the tree the module is compiled against. kernel/sched/core.c
    includes falcon_storage.h under that option, so genksyms folds it into the
    CRC of everything that file exports. ports/koa2/stock-module-abi.md.

cpu_down is the symbol the next stage needs, and no stock module imports it, so
its CRC cannot be checked against the device at all. That is recorded here so
the next person does not assume otherwise.
"""
import importlib.util
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
STOCK = ROOT / 'ports/koa2/materials/2026-11-21/device-evidence/stock-modules'
MODULE = ROOT / 'ports/koa2/materials/2026-11-21/stage-test-kit/koa2_handoff_stage.ko'
FRAMEBUFFER = {'cfb_copyarea', 'cfb_fillrect', 'cfb_imageblit', 'fb_set_var', 'framebuffer_alloc',
               'framebuffer_release', 'lock_fb_info', 'register_framebuffer', 'unregister_framebuffer'}
SCHEDULER = {'set_cpus_allowed_ptr', 'sched_setscheduler', 'wake_up_process'}
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


spec = importlib.util.spec_from_file_location('crcs', ROOT / 'ports/koa2/module-crcs.py')
crcs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crcs)

if not STOCK.is_dir() or not MODULE.is_file():
    print('SKIP needs the device materials of 2026-11-21')
    sys.exit(0)

stock, stock_magic, count = crcs.stock_table([STOCK])
check(count == 63, 'expected 63 stock modules, found %d' % count)
check(stock_magic.startswith('4.1.15-lab126 '), 'stock vermagic is %r' % stock_magic)

# The module the device accepted still agrees with the device.
module = crcs.read(MODULE)
check(crcs.tail(module['vermagic']) == crcs.tail(stock_magic), 'vermagic tails differ')
missing = sorted(set(module['versions']) - set(stock))
differ = sorted(s for s in module['versions'] if s in stock and stock[s] != module['versions'][s])
check(not missing, 'no stock CRC for %s' % missing)
check(not differ, 'the loaded module disagrees with stock about %s' % differ)
check('module_layout' in module['versions'], 'the module has no module_layout entry')
check(len(module['versions']) == 31, 'expected 31 imports, found %d' % len(module['versions']))

# The tree the module is compiled against, and why it does not match everywhere.
abi = pathlib.Path.home() / 'readom-work/out-module-abi/Module.symvers'
smoke = pathlib.Path.home() / 'readom-work/out-headless-smoke/Module.symvers'
if abi.is_file():
    tree = crcs.symvers(abi)
    common = set(tree) & set(stock)
    disagree = {s for s in common if tree[s] != stock[s]}
    check(disagree == FRAMEBUFFER,
          'the module-abi tree should differ from stock only on the framebuffer symbols, not %s'
          % sorted(disagree ^ FRAMEBUFFER))
    # The scheduler symbols are the point of that tree.
    for s in SCHEDULER:
        check(s in tree and s in stock and tree[s] == stock[s],
              '%s should match stock in the module-abi tree' % s)
    # cpu_down cannot be checked against the device, and saying so is the point.
    check('cpu_down' not in stock, 'a stock module now records cpu_down; check it against the tree')
    check('cpu_down' in tree, 'the module-abi tree does not export cpu_down')
if smoke.is_file():
    smoke_tree = crcs.symvers(smoke)
    for s in SCHEDULER:
        check(s in smoke_tree and smoke_tree[s] != stock.get(s),
              '%s was expected to differ from stock without FALCON_WRAPPER' % s)

for failure in failures:
    print('FAIL', failure)
if failures:
    sys.exit(1)
print('stock ABI: the loaded module matches stock on all 31 imports; only the 9 framebuffer '
      'symbols differ, and cpu_down remains uncheckable against the device')
