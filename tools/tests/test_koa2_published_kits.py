#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Every file a published KOA2 device kit lists in its SHA256SUMS is in Git.

On 2026-11-25 the kit in ports/koa2/materials/2026-11-25 was committed without
koa2_handoff_stage.ko: the repository ignores *.ko, so `git add` skipped it
silently, and the sheet's own `sha256sum -c` failed only once it was run in a
fresh clone. This checks the same thing without a clone: for each kit, the
names in SHA256SUMS are exactly the tracked files beside it, and their tracked
contents hash as listed. It also requires the `* -text` rule that keeps
Windows checkouts byte-exact.
"""
import hashlib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MATERIALS = 'ports/koa2/materials'


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT), *args], check=True,
                          capture_output=True).stdout


tracked = git('ls-files', '-z', MATERIALS).decode().split('\0')
kits = sorted({str(Path(p).parent) for p in tracked if p.endswith('/readom-koa2-stage-test/SHA256SUMS')})
assert kits, 'no published kit found'
checked = 0
for kit in kits:
    sums = git('show', f':{kit}/SHA256SUMS').decode()
    listed = {}
    for line in sums.splitlines():
        digest, name = line.split('  ', 1)
        listed[name] = digest
    present = {p[len(kit) + 1:] for p in tracked if p.startswith(kit + '/') and '/' not in p[len(kit) + 1:]}
    missing = sorted(set(listed) - present)
    assert not missing, f'{kit}: listed in SHA256SUMS but not in Git: {missing}'
    unlisted = sorted(present - set(listed) - {'SHA256SUMS', 'config.xml', 'menu.json'})
    assert not unlisted, f'{kit}: tracked but not in SHA256SUMS: {unlisted}'
    for name, digest in listed.items():
        blob = git('show', f':{kit}/{name}')
        assert hashlib.sha256(blob).hexdigest() == digest, f'{kit}/{name}: tracked content differs'
        checked += 1
    menu = git('show', f':{kit}/menu.json').decode()
    import re as _re
    scripts = _re.findall(r'sh \./([A-Za-z0-9_.-]+\.sh)', menu)
    for script in sorted(set(scripts)):
        assert script in listed, f'{kit}: menu.json runs {script} but it is not in the kit'
    attrs = git('check-attr', 'text', '--', f'{kit}/menu.json', f'{kit}/SHA256SUMS').decode()
    assert attrs.count('text: unset') == 2, f'{kit}: needs `* -text` beside it\n{attrs}'
print(f'published KOA2 kits: {len(kits)} kits, {checked} listed files tracked and matching, all byte-exact on checkout, every menu.json script present')
