#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host test for ports/koa2/stage-test/full-quiesce-dryrun.sh."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/full-quiesce-dryrun.sh'


def run_case(expected, fault='', wifi_loaded=False, usb_file='', damaged=False):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for d in ('mnt/us/documents', 'proc', 'sys/bus/i2c/devices/1-0024',
                  'sys/devices/system/cpu/cpu1', 'sys/module/g_mass_storage/parameters'):
            (root / d).mkdir(parents=True, exist_ok=True)
        (root / 'proc/modules').write_text(
            'bcmdhd 1 0 - Live 0\ng_mass_storage 1 0 - Live 0\n' if wifi_loaded
            else 'g_mass_storage 1 0 - Live 0\n'
        )
        (root / 'sys/module/g_mass_storage/parameters/file').write_text(usb_file)
        (root / 'sys/module/g_mass_storage/parameters/removable').write_text('Y\n')
        (root / 'sys/module/g_mass_storage/parameters/stall').write_text('Y\n')
        (root / 'sys/bus/i2c/devices/1-0024/drv_debug').write_text('')
        (root / 'sys/devices/system/cpu/cpu1/online').write_text('1\n')
        ops_log = root / 'ops.log'
        ops_log.write_text('')

        text = re.sub(r'/mnt/us|/proc/|/sys/', lambda m: str(root) + m[0], SCRIPT.read_text())
        rmmod_rc = 1 if fault == 'rmmod_fail' else 0
        modprobe_rc = 1 if fault == 'modprobe_fail' else 0
        mock = f'''uname() {{
    case "$1" in
        -r) echo 4.1.15-lab126 ;;
        -v) echo '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ;;
    esac
}}
sleep() {{ :; }}
sync() {{ :; }}
rmmod() {{ echo "rmmod $*" >> "{ops_log}"; return {rmmod_rc}; }}
modprobe() {{ echo "modprobe $*" >> "{ops_log}"; return {modprobe_rc}; }}
'''
        if fault == 'cpu1_readonly':
            (root / 'sys/devices/system/cpu/cpu1/online').write_text('0\n')
        script = root / 'full-quiesce-dryrun.sh'
        script.write_text(mock + text)
        digest = hashlib.sha256(script.read_bytes()).hexdigest()
        if damaged:
            digest = '0' * 8 + digest[8:]
        (root / 'SHA256SUMS').write_text(f'{digest}  full-quiesce-dryrun.sh\n')

        subprocess.run(['sh', str(script)], check=True, timeout=20)
        base = root / 'mnt/us/koa2-full-quiesce-dryrun'
        report, = base.glob('*/report.txt')
        lines = report.read_text().splitlines()
        assert lines[-1] == 'result=' + expected, (expected, lines)
        assert not (base / '.running').exists(), expected
        return lines, ops_log.read_text().splitlines(), (root / 'sys/devices/system/cpu/cpu1/online').read_text().strip()


lines, ops, cpu1 = run_case('full_quiesce_dryrun_restored')
assert 'step_usb=unloaded' in lines and 'step_touch=slept' in lines and 'step_cpu1=offline' in lines
assert 'restore_cpu1=ok' in lines and 'restore_touch=ok' in lines and 'restore_usb=ok' in lines
assert ops == ['rmmod g_mass_storage', 'modprobe g_mass_storage removable=1'], ops
assert cpu1 == '1'

run_case('step_usb_unload_failed', fault='rmmod_fail')
run_case('dryrun_restore_failed_restart_required', fault='modprobe_fail')
run_case('refused_wifi_loaded', wifi_loaded=True)
run_case('refused_usb_active', usb_file='/dev/loop/0\n')
run_case('refused_cpu1_not_online', fault='cpu1_readonly')
run_case('refused_kit_unverified', damaged=True)

print('KOA2 full-quiesce dry-run: USB+Touch+CPU1 reversible sequence, rollback, and refusals passed')
