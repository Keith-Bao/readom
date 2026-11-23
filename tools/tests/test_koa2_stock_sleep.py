#!/usr/bin/env python3
"""Exercise the paired stock-sleep report against fake clock and input state."""
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/stock-sleep.sh'


def process_stat(start_time):
    return '111 (Xorg) S ' + '0 ' * 18 + str(start_time) + ' 0\n'


def check_result(expected, after_total, rebooted=False, reader_changed=False,
                 node_changed=False, contact=True):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        files = {
            'sys/class/input/event9/device/name': 'cyttsp5_mt\n',
            'dev/input/event9': '',
            'proc/111/comm': 'Xorg\n',
            'proc/111/stat': process_stat(12345),
            'proc/sys/kernel/random/boot_id': 'known-boot\n',
            'suspend-ms': '5000\n',
        }
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        (root / 'mnt/us/documents').mkdir(parents=True)
        (root / 'proc/111/fd').mkdir()
        (root / 'proc/111/fd/6').symlink_to(root / 'dev/input/event9')

        script = root / 'stock-sleep.sh'
        text = re.sub(r'/mnt/us|/proc/|/sys/|/dev/input/',
                      lambda match: str(root) + match[0], SCRIPT.read_text())
        script.write_text("uname() { echo 4.1.15-lab126; }\n" + text)
        helper = root / 'touch-events'
        helper.write_text(f'''#!/bin/sh
if [ "$1" = --suspend-total-ms ]; then
    cat "{root}/suspend-ms"
    exit
fi
printf '%s\n' touch_contact_{'observed' if contact else 'unconfirmed'}
exit {0 if contact else 1}
''')
        helper.chmod(0o755)
        (root / 'SHA256SUMS').write_text(''.join(
            f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n'
            for path in (script, helper)
        ))

        base = root / 'mnt/us/koa2-quiesce-dryrun'
        before = subprocess.run(['sh', str(script), 'before'], timeout=10)
        assert before.returncode == 0, [report.read_text() for report in base.glob('*/report.txt')]
        pending = base / 'STOCK_SLEEP_PENDING.txt'
        report = base / pending.read_text().strip() / 'report.txt'
        (root / 'suspend-ms').write_text(str(after_total) + '\n')
        if rebooted:
            (root / 'proc/sys/kernel/random/boot_id').write_text('new-boot\n')
        if reader_changed:
            (root / 'proc/111/stat').write_text(process_stat(23456))
        if node_changed:
            node = root / 'dev/input/event9'
            node.rename(node.with_suffix('.old'))
            node.touch()
        subprocess.run(['sh', str(script), 'after'], check=True, timeout=10)
        lines = report.read_text().splitlines()
        assert lines[-1] == 'result=' + expected, lines
        assert not pending.exists() and not (base / '.sleep-running').exists()


check_result('stock_sleep_touch_observed', 25000)
check_result('stock_sleep_not_observed', 5001)
check_result('stock_sleep_boot_changed', 25000, rebooted=True)
check_result('stock_sleep_input_changed', 25000, reader_changed=True)
check_result('stock_sleep_input_changed', 25000, node_changed=True)
check_result('stock_sleep_touch_unconfirmed', 25000, contact=False)
print('Stock sleep report distinguishes real suspend, boot changes, Xorg changes and missing touch')
