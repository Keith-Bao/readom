#!/usr/bin/env python3
"""Apply the pinned upstream ioctl xperms change to isolated SELinux research."""
import argparse
import hashlib
import subprocess
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
a = p.parse_args()
if a.source.name not in ('linux-selinux41', 'linux-android41', 'linux-android41-epdc') or not (a.source / '.koa2-headless-source-v1').is_file():
    raise SystemExit('Only prepared linux-selinux41 is allowed')
changes = [
    ('0000-upstream-ioctl-audit.patch', '608ea660d5b3d97fc08451ab7f7bc2e6fe4314ad97330d2628cc163d9d52b13a', '.koa2-ioctl-audit-v1'),
    ('0001-upstream-selinux-ioctl-xperms.patch', '40f071db9174e536732380732bdf739c58f2f56bdebe34dfd821ea2cccb26a14', '.koa2-selinux30-v1'),
]
for name, expected, marker_name in changes:
    patch = Path(__file__).resolve().parent / 'patches' / name
    if hashlib.sha256(patch.read_bytes()).hexdigest() != expected:
        raise SystemExit('Upstream patch checksum mismatch: ' + name)
    marker = a.source / marker_name
    if not marker.exists():
        command = ['patch', '-p1', '--batch', '--fuzz=0', '--no-backup-if-mismatch', '-i', str(patch)]
        subprocess.run(command + ['--dry-run'], cwd=a.source, check=True)
        subprocess.run(command, cwd=a.source, check=True)
        marker.write_text(expected + '\n')
    if marker.read_text().strip() != expected:
        raise SystemExit('SELinux research marker mismatch')
print('Upstream policy30/xperms research patch prepared; runtime tests still required')
