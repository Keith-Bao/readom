#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run ports/koa2/stage-test/full-quiesce-dryrun.sh against a model Kindle.

The script only writes sysfs files, so the model sits behind `cat`: before
every read it applies what was written since -- CPU1 online 0/1, touch
drv_debug 4/5, and the UDC soft_connect disconnect/connect -- the way the
stock drivers do, and the named fault makes one of them misbehave. Nothing
unloads g_mass_storage: USB is stopped by writing the UDC soft_connect node
(udc-core.c usb_gadget_udc_stop), which v3 uses in place of the rmmod that
wedged the real device.
"""
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/full-quiesce-dryrun.sh'
UDC = 'sys/class/udc/ci_hdrc.0'
SOFT = UDC + '/soft_connect'
STATE = UDC + '/state'
TOUCH = 'sys/bus/i2c/devices/1-0024'
LOADED = 'g_mass_storage 1 0 - Live 0\n'


def run(expected, fault='', overrides=None, damaged=False, steps=None):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        files = {
            'proc/modules': LOADED.strip(), 'proc/uptime': '100.00 180.00',
            STATE: 'not attached', SOFT: '',
            TOUCH + '/drv_debug': '', TOUCH + '/sleep_status': 'on',
            'sys/class/regulator/regulator.7/name': 'MAX77796-LSW1',
            'sys/class/regulator/regulator.7/state': 'enabled',
            'sys/devices/system/cpu/online': '0-1',
            'sys/devices/system/cpu/cpu1/online': '1',
            'cpu1.last': '1',
        }
        files.update(overrides or {})
        for name, value in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value + '\n')
        (root / TOUCH / 'driver').mkdir()
        (root / SOFT).parent.mkdir(parents=True, exist_ok=True)
        (root / 'mnt/us/documents').mkdir(parents=True)
        ops = root / 'ops.log'
        ops.write_text('')

        r = str(root)
        mock = f'''uname() {{
    case "$1" in
        -r) echo 4.1.15-lab126 ;;
        -v) echo '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ;;
    esac
}}
sleep() {{ [ "$fault" = usb_stop_fails ] && echo configured > "{r}/{STATE}"; }}
sync() {{ :; }}
fault='{fault}'
model() {{
    soft=$(command cat "{r}/{SOFT}")
    if [ -n "$soft" ]; then
        echo "soft_connect $soft" >> "{r}/ops.log"
        : > "{r}/{SOFT}"
        echo "not attached" > "{r}/{STATE}" 
    fi
    debug=$(command cat "{r}/{TOUCH}/drv_debug")
    if [ -n "$debug" ]; then
        echo "drv_debug $debug" >> "{r}/ops.log"
        : > "{r}/{TOUCH}/drv_debug"
        if [ "$debug" = 4 ]; then
            echo off > "{r}/{TOUCH}/sleep_status"
            [ "$fault" = power_stays_on ] || echo disabled > "{r}/sys/class/regulator/regulator.7/state"
        elif [ "$debug" = 5 ] && [ "$fault" != wake_fails ]; then
            echo on > "{r}/{TOUCH}/sleep_status"
            echo enabled > "{r}/sys/class/regulator/regulator.7/state"
        fi
    fi
    cpu1=$(command cat "{r}/sys/devices/system/cpu/cpu1/online")
    last=$(command cat "{r}/cpu1.last" 2>/dev/null)
    if [ "$cpu1" != "$last" ]; then
        echo "cpu1 $cpu1" >> "{r}/ops.log"
        if [ "$cpu1" = 0 ] && [ "$fault" = cpu1_stays_online ]; then cpu1=1; fi
        if [ "$cpu1" = 1 ] && [ "$last" = 0 ] && [ "$fault" = cpu1_restore_fails ]; then cpu1=0; fi
        echo "$cpu1" > "{r}/sys/devices/system/cpu/cpu1/online"
        echo "$cpu1" > "{r}/cpu1.last"
        if [ "$cpu1" = 1 ]; then echo 0-1; else echo 0; fi > "{r}/sys/devices/system/cpu/online"
    fi
}}
cat() {{ model; command cat "$@"; }}
'''
        text = re.sub(r'/mnt/us|/proc/|/sys/', lambda m: r + m[0], SCRIPT.read_text())
        script = root / 'full-quiesce-dryrun.sh'
        script.write_text(mock + text)
        digest = hashlib.sha256(script.read_bytes()).hexdigest()
        if damaged:
            digest = '0' * 8 + digest[8:]
        (root / 'SHA256SUMS').write_text(f'{digest}  full-quiesce-dryrun.sh\n')

        subprocess.run(['sh', str(script)] + ([steps] if steps else []), check=True, timeout=30)
        base = root / 'mnt/us/koa2-full-quiesce-dryrun'
        report, = base.glob('*/report.txt')
        lines = report.read_text().splitlines()
        assert lines[-1] == 'result=' + expected, (expected, lines)
        assert not (base / '.running').exists(), expected
        state = {
            'cpus': (root / 'sys/devices/system/cpu/online').read_text().strip(),
            'touch': (root / TOUCH / 'sleep_status').read_text().strip(),
            'modules': (root / 'proc/modules').read_text().strip(),
        }
        return lines, ops.read_text().splitlines(), state


def stages(lines):
    return [line.split()[0][len('stage='):] for line in lines if line.startswith('stage=')]


FULL = ['drv_debug 4', 'cpu1 0', 'cpu1 1', 'drv_debug 5']
RESTORED = {'cpus': '0-1', 'touch': 'on', 'modules': LOADED.strip()}

lines, ops, state = run('full_quiesce_dryrun_restored')
assert ops == FULL, ops
assert state == RESTORED, state
assert stages(lines) == ['usb_verify_idle', 'touch_sleep', 'cpu1_offline', 'hold',
                         'restore_cpu1', 'restore_touch', 'restore_usb', 'done'], lines
for wanted in ('step_usb=quiesced_idle', 'step_touch=slept', 'step_cpu1=offline',
               'touch_power_asleep=off_disabled', 'touch_power_restored=on_enabled',
               'restore_cpu1=ok', 'restore_touch=ok', 'restore_usb=ok'):
    assert wanted in lines, (wanted, lines)
# The module is never unloaded, even across a full run.
assert state['modules'] == LOADED.strip(), state
# Every stage line comes before the action it names, so after a freeze the last
# one in the report is the step that did not return.
assert lines.index('step_usb=quiesced_idle') > [i for i, l in enumerate(lines) if l.startswith('stage=usb_verify_idle')][0]

# Pairs, to isolate a combination: only the named steps run and only their
# preconditions are checked.
lines, ops, state = run('full_quiesce_dryrun_restored', steps='touch,cpu1',
                        overrides={STATE: 'configured'})  # host attached, but USB step is skipped
assert ops == ['drv_debug 4', 'cpu1 0', 'cpu1 1', 'drv_debug 5'], ops
assert 'steps=touch,cpu1' in lines and state == RESTORED, (lines, state)
lines, ops, state = run('full_quiesce_dryrun_restored', steps='usb,touch',
                        overrides={'sys/devices/system/cpu/online': '0',
                                   'sys/devices/system/cpu/cpu1/online': '0', 'cpu1.last': '0'})
assert ops == ['drv_debug 4', 'drv_debug 5'], ops
for bad in ('cpu1,usb', 'all', 'usb;reboot'):
    _, ops, _ = run('refused_unknown_steps', steps=bad)
    assert ops == [], (bad, ops)

# A failing step stops the sequence; every earlier step is still restored.
lines, ops, state = run('step_usb_not_stopped', 'usb_stop_fails')
assert ops == FULL, ops
assert state == RESTORED, state
_, ops, state = run('step_touch_not_powered_down', 'power_stays_on')
assert ops == ['drv_debug 4', 'drv_debug 5'], ops
assert state['cpus'] == '0-1' and state['modules'] == LOADED.strip(), state
_, ops, state = run('step_cpu1_offline_failed', 'cpu1_stays_online')
assert ops == [op for op in FULL if op != 'cpu1 1'], ops  # it never went offline
assert state == RESTORED, state

# A failing restore is reported and does not stop the later ones.
lines, ops, state = run('dryrun_restore_failed_restart_required', 'cpu1_restore_fails')
assert 'restore_cpu1=failed' in lines and 'restore_touch=ok' in lines and 'restore_usb=ok' in lines, lines
assert ops == FULL, ops
lines, ops, _ = run('dryrun_restore_failed_restart_required', 'wake_fails')
assert 'restore_touch=failed' in lines and 'restore_usb=ok' in lines, lines

# Refusals change nothing.
for expected, kwargs in (
        ('refused_kit_unverified', {'damaged': True}),
        ('refused_wifi_loaded', {'overrides': {'proc/modules': 'bcmdhd 1 0 - Live 0\n' + LOADED.strip()}}),
        ('refused_usb_module_missing_or_busy', {'overrides': {'proc/modules': LOADED.replace('1 0', '1 1').strip()}}),
        ('refused_usb_host_connected', {'overrides': {STATE: 'configured'}}),
        ('refused_touch_not_awake', {'overrides': {TOUCH + '/sleep_status': 'off'}}),
        ('refused_touch_not_awake', {'overrides': {'sys/class/regulator/regulator.7/state': 'disabled'}}),
        ('refused_cpu1_not_online', {'overrides': {'sys/devices/system/cpu/cpu1/online': '0',
                                                   'sys/devices/system/cpu/online': '0',
                                                   'cpu1.last': '0'}})):
    _, ops, _ = run(expected, **kwargs)
    assert all(not op.startswith(('soft_connect', 'drv_debug')) for op in ops), (expected, ops)
    assert 'cpu1 0' not in ops, (expected, ops)

print('KOA2 full-quiesce dry-run: USB soft-disconnect + Touch + CPU1 in order with a synced '
      'stage line before each, reverse restore per step, module never unloaded, refusals safe')
