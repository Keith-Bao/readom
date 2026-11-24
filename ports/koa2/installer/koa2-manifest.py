#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Create or verify a KOA2 self-use package manifest (version, target, SHA256SUMS)."""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def cmd_create(args):
    pkg = Path(args.dir)
    files = {}
    for p in sorted(pkg.rglob('*')):
        if p.is_file() and p.name not in ('manifest.json', 'SHA256SUMS'):
            rel = p.relative_to(pkg).as_posix()
            files[rel] = sha256_file(p)
    manifest = {
        'model': 'KOA2',
        'firmware': '5.16.2.1.1',
        'kernel_release': '4.1.15-lab126',
        'version': args.version,
        'files': files,
    }
    (pkg / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')
    sums = ''.join(f'{digest}  {rel}\n' for rel, digest in sorted(files.items()))
    (pkg / 'SHA256SUMS').write_text(sums)
    print(f'manifest created: {pkg}/manifest.json ({len(files)} files, version={args.version})')
    return 0


def cmd_verify(args):
    pkg = Path(args.dir)
    m = json.loads((pkg / 'manifest.json').read_text())
    if m.get('model') != 'KOA2' or m.get('kernel_release') != '4.1.15-lab126':
        print('target mismatch', file=sys.stderr)
        return 2
    for rel, expected in m.get('files', {}).items():
        fp = pkg / rel
        if not fp.is_file() or sha256_file(fp) != expected:
            print(f'checksum mismatch: {rel}', file=sys.stderr)
            return 1
    print(f'manifest verified: {pkg} (version={m.get("version")}, {len(m["files"])} files OK)')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('create')
    c.add_argument('--dir', required=True)
    c.add_argument('--version', required=True)
    v = sub.add_parser('verify')
    v.add_argument('--dir', required=True)
    args = ap.parse_args()
    return cmd_create(args) if args.cmd == 'create' else cmd_verify(args)


if __name__ == '__main__':
    sys.exit(main())
