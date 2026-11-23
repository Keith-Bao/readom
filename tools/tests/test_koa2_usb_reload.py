#!/usr/bin/env python3
"""Run the USB-only KUAL test against mock Kindle state and module commands."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/usb-reload.sh'
GIT_BASH = Path(r'C:\Program Files\Git\bin\bash.exe')
PARAM = 'sys/module/g_mass_storage/parameters'
UDC = 'sys/class/udc/ci_hdrc.0'
LUN = UDC + '/device/gadget/lun0/file'
LOADED = 'g_mass_storage 1 0 - Live 0\n'


def verify(expected, actions='', overrides=None, restore='restore', damaged=False):
    with tempfile.TemporaryDirectory(dir=ROOT if os.name == 'nt' else None) as temporary:
        path = Path(temporary)
        root = path.as_posix()
        if os.name == 'nt':
            root = f'/{path.drive[0].lower()}/{root[3:]}'
        parameters = {
            'file': '', 'luns': '0', 'removable': 'Y', 'stall': 'Y', 'recovery_mode': '0',
            'ro': '', 'cdrom': '', 'nofua': '', 'idVendor': '6473', 'idProduct': '4',
            'bcdDevice': '0', 'iManufacturer': '(null)', 'iProduct': '(null)', 'iSerialNumber': '(null)',
        }
        files = {f'{PARAM}/{name}': value for name, value in parameters.items()}
        files.update({'proc/modules': LOADED, UDC + '/state': '0', LUN: ''})
        files.update(overrides or {})
        for name, value in files.items():
            file = path / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(value + '\n', newline='\n')
        (path / 'mnt/us/documents').mkdir(parents=True)
        mock = f'''
uname() {{
    case "$1" in
        -r) echo 4.1.15-lab126 ;;
        -v) echo '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ;;
    esac
}}
rmmod() {{
    echo "rmmod $*" >> "{root}/calls"
    : > "{root}/proc/modules"
    rm -f "{root}/{LUN}"
}}
modprobe() {{
    echo "modprobe $*" >> "{root}/calls"
    if [ '{restore}' = fail ]; then return 1; fi
    printf '%s' 'g_mass_storage 1 0 - Live 0' > "{root}/proc/modules"
    if [ '{restore}' = restore ]; then : > "{root}/{LUN}"; fi
}}
'''
        text = SCRIPT.read_text()
        for prefix in ('/mnt/us', '/sys/', '/proc/'):
            text = text.replace(prefix, root + prefix)
        script = path / 'usb-reload.sh'
        script.write_text(mock + text, newline='\n')
        digest = hashlib.sha256(script.read_bytes()).hexdigest()
        (path / 'SHA256SUMS').write_text(f"{'0' * 64 if damaged else digest}  usb-reload.sh\n", newline='\n')
        shell = str(GIT_BASH) if os.name == 'nt' and GIT_BASH.exists() else shutil.which('sh')
        subprocess.run([shell, root + '/usb-reload.sh'], check=True, timeout=15)
        base = path / 'mnt/us/koa2-usb-reload'
        report, = base.glob('*/report.txt')
        lines = report.read_text().splitlines()
        assert lines[-1] == f'result={expected}', lines
        call_log = path / 'calls'
        assert (call_log.read_text() if call_log.exists() else '') == actions
        assert not (base / '.running').exists()


RELOAD = 'rmmod g_mass_storage\nmodprobe g_mass_storage removable=1 idVendor=6473 idProduct=4 bcdDevice=0\n'
verify('usb_idle_rebound_host_check_pending', RELOAD)
verify('refused_lun_not_proven_idle', overrides={UDC + '/state': '1'})
verify('refused_lun_not_proven_idle', overrides={LUN: '/dev/loop/0'})
verify('refused_module_missing_or_busy', overrides={'proc/modules': LOADED.replace('1 0', '1 1')})
verify('refused_kit_unverified', damaged=True)
verify('usb_reload_failed_restart_required', RELOAD, restore='fail')
verify('usb_rebind_unverified_restart_required', RELOAD, restore='unbound')
print('KOA2 USB-only reload: guarded removal, parameter-preserving reload, and failed recovery reporting passed')
