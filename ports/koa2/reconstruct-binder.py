#!/usr/bin/env python3
"""Reconstruct the repository's reviewed Binder implementation for 4.1 port research.

Only exact Binder file sections are extracted. No patches to 3.0 core headers,
security hooks or fdtable are applied to a 4.1 tree.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('destination', type=Path)
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
names = ['0001-aosp30-binder-ashmem-selinux-hooks.patch',
         '0003-rinando-oreo-binder-contexts-sg.patch',
         '0900-binder-poll-waitqueue-lifetime.patch',
         '0901-binder-private-buffer-metadata.patch']
allowed = {'drivers/staging/android/' + name for name in
           ('binder.c', 'binder.h', 'binder_trace.h', 'uapi/binder.h')}
if a.destination.exists() and any(a.destination.iterdir()):
    raise SystemExit('Refusing nonempty reconstruction directory')
a.destination.mkdir(parents=True, exist_ok=True)
(a.destination / 'drivers/staging/android/uapi').mkdir(parents=True)
receipt = []
for index, name in enumerate(names):
    original = (repo / 'patches/android-core' / name).read_bytes()
    text = original.decode().replace('\r\n', '\n')
    starts = [m.start() for m in re.finditer(r'^--- (?:a/[^\n]+|/dev/null)\n\+\+\+ b/[^\n]+\n', text, re.M)]
    selected = []
    for i, start in enumerate(starts):
        section = text[start: starts[i+1] if i+1 < len(starts) else len(text)]
        target = section.splitlines()[1][6:]
        if target in allowed:
            selected.append(section)
    if not selected:
        raise SystemExit('Expected Binder changes missing: ' + name)
    patch = a.destination / f'{index:02d}-selected.patch'
    patch.write_text(''.join(selected))
    subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '--no-backup-if-mismatch', '-i', str(patch.resolve())], cwd=a.destination, check=True)
    receipt.append({'patch': name, 'sha256': hashlib.sha256(original).hexdigest()})
files = {name: hashlib.sha256((a.destination / name).read_bytes()).hexdigest() for name in sorted(allowed)}
(a.destination / 'receipt.json').write_text(json.dumps({'patches': receipt, 'files': files,
    'scope': 'Binder research snapshot, not proven Linux 4.1 compatibility. 3.0 fdtable compatibility patch intentionally excluded.'}, indent=2) + '\n')
print('Binder research snapshot reconstructed; see receipt.json')
