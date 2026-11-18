#!/usr/bin/env python3
"""Exercise real setlocalversion scripts with stock/experimental configs."""
import argparse
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('stock', type=Path)
p.add_argument('experimental', type=Path)
a = p.parse_args()
cases = [(a.stock, '', '-lab126'),
         (a.stock, '-koa2-headless-smoke', '-lab126'),
         (a.experimental, '', '-lab126'),
         (a.experimental, '-koa2-headless-smoke', '-lab126-koa2-headless-smoke')]
for source, local, expected in cases:
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp) / 'include/config'
        directory.mkdir(parents=True)
        (directory / 'auto.conf').write_text(f'CONFIG_LOCALVERSION="{local}"\nCONFIG_LOCALVERSION_AUTO=n\n')
        value = subprocess.check_output(['sh', str(source / 'scripts/setlocalversion'), str(source)], cwd=tmp, text=True).strip()
        if value != expected:
            raise SystemExit(f'{source}: {local!r}: expected {expected!r}, got {value!r}')
print('4 version-isolation cases passed')
