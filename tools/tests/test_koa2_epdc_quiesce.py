#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host test for ports/koa2/stage-test/epdc-quiesce.sh v3 against a simulated system.

v2 stops and starts the stock x Upstart job (Xorg, awesome, blanket) rather
than lab126_gui, writes a synced stage= line before each action, and waits
up to 30 s for Xorg to reopen /dev/fb0. v3 starts x with --no-wait (a blocking
`initctl start x` hung on the device) and logs x / lab126_gui status after.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/epdc-quiesce.sh'

SIM = r'''
import json, sys
from pathlib import Path

root = Path(sys.argv[1])
cmd = sys.argv[2]
args = sys.argv[3:]
state = json.loads((root / 'state.json').read_text())
fault = state['fault']
state['ops'].append([cmd] + args)

def sync_fs():
    mod_file = root / 'proc/modules'
    fb_dir = root / 'sys/class/graphics/fb0'
    fb_dev = root / 'dev/fb0'
    xorg_fd = root / 'proc/111/fd/3'
    if state['mod_loaded']:
        mod_file.write_text(f"mxc_epdc_v2_fb 138292 {state['users']} - Live 0x7f000000\n")
        fb_dir.mkdir(parents=True, exist_ok=True)
        (fb_dir / 'name').write_text('mxc_epdc_fb\n')
        fb_dev.parent.mkdir(parents=True, exist_ok=True)
        fb_dev.touch()
    else:
        mod_file.write_text("cyttsp5_i2c 1000 0 - Live 0x7f010000\n")
        if (fb_dir / 'name').exists():
            (fb_dir / 'name').unlink()
        if fb_dir.exists():
            fb_dir.rmdir()
        if fb_dev.exists():
            fb_dev.unlink()
    if state['x_running'] and state['mod_loaded'] and fault != 'gui_no_fb_reopen':
        xorg_fd.parent.mkdir(parents=True, exist_ok=True)
        (root / 'proc/111/comm').write_text('Xorg\n')
        if xorg_fd.exists() or xorg_fd.is_symlink():
            xorg_fd.unlink()
        xorg_fd.symlink_to(fb_dev)
    else:
        if xorg_fd.exists() or xorg_fd.is_symlink():
            xorg_fd.unlink()

rc = 0
if cmd == 'initctl':
    action = args[0]
    rest = [a for a in args[1:] if not a.startswith('--')]
    job = rest[0]
    if action == 'start' and '--no-wait' not in args:
        # A blocking start is what hung on the real device.
        state['blocking_start'] = True
    if action == 'status':
        if fault == 'x_missing' or job not in ('x', 'lab126_gui'):
            rc = 1
        else:
            st = 'start/running' if state['x_running'] else 'stop/waiting'
            print(f'{job} {st}')
    elif action == 'stop':
        state['x_running'] = False
        if fault != 'users_stuck':
            state['users'] = 0
    elif action == 'start':
        if fault == 'x_start_fails':
            rc = 1
        else:
            state['x_running'] = True
            if state['mod_loaded']:
                state['users'] = 3
elif cmd == 'rmmod':
    if fault == 'rmmod_fails':
        rc = 1
    else:
        state['mod_loaded'] = False
elif cmd == 'insmod':
    if fault == 'reload_fails':
        rc = 1
    else:
        state['mod_loaded'] = True
        state['reloaded_args'] = args

sync_fs()
(root / 'state.json').write_text(json.dumps(state))
sys.exit(rc)
'''


def run_case(expected, fault='', release='4.1.15-lab126', damaged=False,
             mod_loaded=True, has_ko=True, waveform='320_R198_TEST.wrf.gz'):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for d in ('mnt/us/documents', 'proc/111/fd', 'sys/class/graphics/fb0',
                  'sys/module/mxc_epdc_v2_fb/parameters', 'lib/modules', 'dev'):
            (root / d).mkdir(parents=True, exist_ok=True)
        (root / 'proc/111/comm').write_text('Xorg\n')
        (root / 'dev/fb0').touch()
        (root / 'proc/111/fd/3').symlink_to(root / 'dev/fb0')
        if has_ko:
            (root / 'lib/modules/mxc_epdc_v2_fb.ko').write_bytes(b'ELF')
        if waveform is not None:
            (root / 'sys/module/mxc_epdc_v2_fb/parameters/waveform').write_text(waveform + '\n')
        (root / 'proc/modules').write_text(
            'mxc_epdc_v2_fb 138292 3 - Live 0x7f000000\n' if mod_loaded else 'other 1 0 - Live 0\n'
        )
        (root / 'sys/class/graphics/fb0/name').write_text('mxc_epdc_fb\n')
        (root / 'state.json').write_text(json.dumps({
            'fault': fault, 'mod_loaded': mod_loaded, 'x_running': True,
            'users': 3 if mod_loaded else 0, 'ops': [], 'reloaded_args': [],
        }))
        (root / 'sim.py').write_text(SIM)

        text = re.sub(r'/mnt/us|/proc/|/sys/|/lib/modules|/dev/fb0',
                      lambda m: str(root) + m[0], SCRIPT.read_text())
        mock = f'''uname() {{
    case "$1" in
        -r) echo '{release}' ;;
        -v) echo '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ;;
    esac
}}
sleep() {{ :; }}
sync() {{ :; }}
initctl() {{ "{sys.executable}" "{root}/sim.py" "{root}" initctl "$@"; }}
rmmod() {{ "{sys.executable}" "{root}/sim.py" "{root}" rmmod "$@"; }}
insmod() {{ "{sys.executable}" "{root}/sim.py" "{root}" insmod "$@"; }}
'''
        script = root / 'epdc-quiesce.sh'
        script.write_text(mock + text)
        digest = hashlib.sha256(script.read_bytes()).hexdigest()
        if damaged:
            digest = '0' * 8 + digest[8:]
        (root / 'SHA256SUMS').write_text(f'{digest}  epdc-quiesce.sh\n')

        subprocess.run(['sh', str(script)], check=True, timeout=20)
        base = root / 'mnt/us/koa2-epdc-quiesce'
        report, = base.glob('*/report.txt')
        lines = report.read_text().splitlines()
        assert lines[-1] == 'result=' + expected, (expected, lines)
        assert not (base / '.running').exists(), expected
        state = json.loads((root / 'state.json').read_text())
        return lines, state


# 1. Happy path: exact waveform preserved on reload, GUI restarted, fb0 restored.
lines, st = run_case('epdc_quiesce_restored')
assert 'epdc_unloaded=1' in lines and 'epdc_reloaded=1' in lines and 'fb_restored=1' in lines, lines
assert any(a == 'waveform=320_R198_TEST.wrf.gz' for a in st['reloaded_args']), st['reloaded_args']
assert st['mod_loaded'] and st['x_running'] and st['users'] == 3, st
assert 'x_started=1' in lines and any(l.startswith('x_status_before=') for l in lines), lines
assert [op for op in st['ops'] if op[0] == 'initctl' and op[1] != 'status'] == [['initctl', 'stop', 'x'], ['initctl', 'start', '--no-wait', 'x']], st['ops']
assert not st.get('blocking_start'), st
assert 'x_status_after=x start/running' in lines and any(l.startswith('lab126_gui_status_after=') for l in lines), lines
stages = [l.split()[0] for l in lines if l.startswith('stage=')]
assert stages == ['stage=stop_x', 'stage=rmmod', 'stage=restore_insmod', 'stage=restore_x', 'stage=done'], stages

# 2. Failure paths: every path restores GUI / module safely.
_, st = run_case('epdc_users_not_released', fault='users_stuck')
assert st['x_running'] and st['mod_loaded'] and not any(op[0] == 'rmmod' for op in st['ops']), st

_, st = run_case('epdc_rmmod_failed', fault='rmmod_fails')
assert st['x_running'] and st['mod_loaded'], st

_, st = run_case('epdc_reload_failed_restart_required', fault='reload_fails')
assert st['x_running'] and not st['mod_loaded'], st

lines, _ = run_case('epdc_x_start_failed_restart_required', fault='x_start_fails')
assert 'x_started=0' in lines, lines
lines, _ = run_case('epdc_gui_restore_timeout', fault='gui_no_fb_reopen')
assert 'fb_restored=0' in lines, lines

# 3. Refusal paths: zero mutations issued.
for exp, kw in (
    ('refused_unknown_kernel', {'release': '5.10.0-other'}),
    ('refused_kit_unverified', {'damaged': True}),
    ('refused_epdc_not_loaded', {'mod_loaded': False}),
    ('refused_epdc_ko_missing', {'has_ko': False}),
    ('refused_epdc_params_missing', {'waveform': None}),
    ('refused_epdc_params_missing', {'waveform': ''}),
    ('refused_x_not_running', {'fault': 'x_missing'}),
):
    _, st = run_case(exp, **kw)
    assert not any(op[0] in ('rmmod', 'insmod') or op[:2] == ['initctl', 'stop'] for op in st['ops']), (exp, st)

print('KOA2 EPDC-only stop/reload: parameter-preserving reload, rollback on every failure, and safe refusals passed')
