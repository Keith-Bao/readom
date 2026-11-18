#!/usr/bin/env python3
"""Check immutable stock-kernel configuration barriers for the agreed Android target.

This checks prerequisites, not complete Android compatibility. Exit 2 means a
new kernel is required by at least one checked built-in prerequisite.
"""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('config', type=Path)
args = parser.parse_args()
values = {}
for line in args.config.read_text().splitlines():
    match = re.fullmatch(r'(CONFIG_\w+)=(.*)', line)
    disabled = re.fullmatch(r'# (CONFIG_\w+) is not set', line)
    if match:
        values[match[1]] = match[2]
    elif disabled:
        values[disabled[1]] = 'n'
checks = {
    'CONFIG_NAMESPACES': 'mount/process isolation required by the selected Android design',
    'CONFIG_SECCOMP': 'Android process syscall filtering',
    'CONFIG_SECURITY': 'Linux security hooks for the agreed SELinux target',
    'CONFIG_SECURITY_SELINUX': 'SELinux enforcing acceptance criterion',
}
barriers = [{'option': key, 'actual': values.get(key, 'absent'), 'reason': reason}
            for key, reason in checks.items() if values.get(key) != 'y']
print(json.dumps({'built_in_prerequisites': barriers,
                  'stock_modules_only_route': 'rejected' if barriers else 'not_ruled_out',
                  'scope': 'Does not test Binder, drivers, boot, or complete Android runtime.'}, indent=2))
raise SystemExit(2 if barriers else 0)
