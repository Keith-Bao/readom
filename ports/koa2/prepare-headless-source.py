#!/usr/bin/env python3
"""Prepare a separate, verified source tree for the experimental kernel."""
import argparse
import hashlib
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('archive', type=Path)
p.add_argument('destination', type=Path)
a = p.parse_args()
expected = 'efb1b966fedcba318af381080f6a699642a66a929c42e8578aeb6e3fe240ea93'
if hashlib.sha256(a.archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('Stock kernel archive checksum mismatch')
marker = a.destination / '.koa2-headless-source-v1'
original = 'res="-lab126"\necho "$res"'
replacement = 'res="-lab126"\nif test "$CONFIG_LOCALVERSION" = "-koa2-headless-smoke"; then\n\tres="${res}${CONFIG_LOCALVERSION}"\nfi\necho "$res"'
if not marker.exists():
    if a.destination.exists() and any(a.destination.iterdir()):
        raise SystemExit('Refusing to modify an existing unverified source tree')
    a.destination.mkdir(parents=True, exist_ok=True)
    subprocess.run(['tar', '--no-same-owner', '-xzf', str(a.archive), '-C', str(a.destination)], check=True)
    target = a.destination / 'scripts/setlocalversion'
    text = target.read_text()
    if text.count(original) != 1:
        raise SystemExit('Unexpected stock setlocalversion; no patch applied')
    target.write_text(text.replace(original, replacement))
    marker.write_text(expected + '\n')
if marker.read_text().strip() != expected or replacement not in (a.destination / 'scripts/setlocalversion').read_text():
    raise SystemExit('Prepared source identity patch verification failed')
print('Verified experimental source:', a.destination)
