#!/usr/bin/env python3
"""Check actual generated release and every module's embedded vermagic."""
import argparse
import json
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
expected = '4.1.15-lab126-koa2-headless-smoke'
actual = (a.output / 'include/config/kernel.release').read_text().strip()
modules = []
for path in sorted(a.output.rglob('*.ko')):
    # Host kmod omits fields for some ARM ELF files despite their presence in
    # .modinfo. Inspect the target ELF section directly instead.
    section = subprocess.check_output(['arm-linux-gnueabi-readelf', '-p', '.modinfo', str(path)], text=True)
    matches = re.findall(r'\bvermagic=([^\r\n]*)', section)
    value = matches[0].strip() if len(matches) == 1 else ''
    modules.append({'module': str(path.relative_to(a.output)), 'vermagic': value,
                    'passed': value.split()[0] == expected if value else False})
passed = actual == expected and bool(modules) and all(m['passed'] for m in modules)
print(json.dumps({'expected': expected, 'release': actual, 'module_count': len(modules),
                  'passed': passed, 'modules': modules,
                  'scope': 'Identity only; does not establish symbol, relocation, boot or hardware compatibility.'}, indent=2))
raise SystemExit(0 if passed else 2)
