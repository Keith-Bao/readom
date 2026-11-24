#!/usr/bin/env python3
"""Run the touch-only sleep and wake test against a simulated stock driver.

The script on the device writes drv_debug and then sleeps; here `sleep` runs a
small model of the controller instead. The model applies whatever command was
written (4 = stock sleep, 5 = stock wake), advances the interrupt counters as
the real bus would, and appends the driver's log lines, so each failure the
device script has to tell apart can be produced on purpose.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/touch-quiesce.sh'
TOUCH = 'sys/bus/i2c/devices/1-0024'

MODEL = r'''
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
touch = root / 'sys/bus/i2c/devices/1-0024'
state = json.loads((root / 'state.json').read_text())
fault = state['fault']
command = (touch / 'drv_debug').read_text().strip()
(touch / 'drv_debug').write_text('')
log = []
if command:
    state['commands'].append(command)
if command == '4':
    state['awake'] = False
    if fault == 'sleep_logged_failure':
        log.append('cyttsp5_drv_debug_store: Suspend failed rc=1')
    else:
        log.append('cyttsp5_drv_debug_store: Suspend succeeded')
    state['powered'] = fault == 'power_stays_on'
elif command == '5':
    log.append('cyttsp5_drv_debug_store: Resume succeeded')
    if fault != 'wake_fails':
        state['awake'] = state['powered'] = True
elif not state['awake']:
    # A sleep call inside the asleep window: the faults that happen there.
    if fault == 'bus_leaks':
        state['i2c'] += 2
    if fault == 'system_suspend':
        state['suspend_ms'] += 4000
    if fault == 'screen_blank':
        log.append('cyttsp5_i2c_adapter 1-0024: fb_notifier_callback: POWERDOWN!')
    if fault == 'screen_unblank':
        log.append('cyttsp5_i2c_adapter 1-0024: fb_notifier_callback: UNBLANK!')
    if fault == 'reader_changes':
        state['reader_start'] = 999
if state['awake'] and fault != 'no_awake_activity':
    state['i2c'] += 3
(root / 'proc/interrupts').write_text(
    '           CPU0       CPU1\n'
    ' 36: %10d          0       GIC  36 Level     30a30000.i2c\n'
    ' 58: %10d          0  gpio-mxc  10 Edge      1-0024\n' % (state['i2c'], state['touch']))
(touch / 'sleep_status').write_text('on\n' if state['awake'] else 'off\n')
(root / 'sys/class/regulator/regulator.7/state').write_text(
    'enabled\n' if state['powered'] else 'disabled\n')
gpio_level = 'lo' if fault == 'gpio_psr_always_lo' else ('hi' if state['powered'] else 'lo')
(root / 'sys/kernel/debug/gpio').write_text(
    ' gpio-12  (touch_1v8           ) out %s    \n' % gpio_level)
thread = 'D' if fault == 'thread_busy' and not state['awake'] else 'S'
(root / 'proc/222/stat').write_text('222 (irq/58-1-0024) %s 2 0 0\n' % thread)
(root / 'proc/111/stat').write_text('111 (Xorg) S ' + '0 ' * 18 + str(state['reader_start']) + ' 0\n')
(root / 'suspend-ms').write_text('%d\n' % state['suspend_ms'])
with open(root / 'messages', 'a') as messages:
    for line in log:
        messages.write('[100.0] ' + line + '\n')
(root / 'state.json').write_text(json.dumps(state))
'''


def run(expected, fault='', commands=('4', '5'), contact=True, damaged=False,
        other_user=False, asleep_at_start=False):
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for name in ('mnt/us/documents', TOUCH + '/driver', 'proc/111/fd', 'proc/222/fd',
                     'proc/333/fd', 'sys/class/input/event9/device',
                     'sys/class/regulator/regulator.7', 'sys/kernel/debug', 'dev/input'):
            (root / name).mkdir(parents=True, exist_ok=True)
        files = {
            TOUCH + '/drv_debug': '',
            'sys/class/input/event9/device/name': 'cyttsp5_mt\n',
            'sys/class/regulator/regulator.7/name': 'MAX77796-LSW1\n',
            'dev/input/event9': '', 'dev/i2c-1': '', 'proc/touch': '',
            'proc/111/comm': 'Xorg\n', 'proc/222/comm': 'irq/58-1-0024\n',
            'proc/333/comm': 'tool\n', 'messages': '',
        }
        for name, content in files.items():
            (root / name).write_text(content)
        (root / 'proc/111/fd/6').symlink_to(root / 'dev/input/event9')
        if other_user:
            (root / 'proc/333/fd/3').symlink_to(root / 'dev/i2c-1')
        awake = not asleep_at_start
        (root / 'state.json').write_text(json.dumps({
            'fault': fault, 'commands': [], 'awake': awake, 'powered': awake,
            'i2c': 500, 'touch': 40, 'suspend_ms': 5000, 'reader_start': 12345}))
        (root / 'model.py').write_text(MODEL)
        subprocess.run([sys.executable, str(root / 'model.py'), str(root)], check=True)

        text = re.sub(r'/mnt/us|/proc/|/sys/|/dev/input/|/dev/i2c-1',
                      lambda match: str(root) + match[0], SCRIPT.read_text())
        mock = f'''uname() {{
    case "$1" in
        -r) echo 4.1.15-lab126 ;;
        -v) echo '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ;;
    esac
}}
dmesg() {{ cat "{root}/messages"; }}
sleep() {{ "{sys.executable}" "{root}/model.py" "{root}"; }}
'''
        script = root / 'touch-quiesce.sh'
        script.write_text(mock + text)
        helper = root / 'touch-events'
        result = 'observed' if contact else 'no_contact_observed'
        helper.write_text(f'''#!/bin/sh
if [ "$1" = --suspend-total-ms ]; then
    cat "{root}/suspend-ms"
    exit
fi
echo touch_contact_{result}
exit {0 if contact else 1}
''')
        helper.chmod(0o755)
        sums = ''.join(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n'
                       for path in (script, helper))
        if damaged:
            sums = '0' * 8 + sums[8:]
        (root / 'SHA256SUMS').write_text(sums)

        subprocess.run(['sh', str(script)], check=True, timeout=30)
        base = root / 'mnt/us/koa2-touch-quiesce'
        report, = base.glob('*/report.txt')
        lines = report.read_text().splitlines()
        assert lines[-1] == 'result=' + expected, lines
        state = json.loads((root / 'state.json').read_text())
        assert state['commands'] == list(commands), (expected, state['commands'])
        assert not (base / '.running').exists(), expected
        if commands:
            # Whatever happened while it slept, one wake was attempted.
            assert ('wake=ok' in lines) == (fault != 'wake_fails'), lines
        return lines


lines = run('touch_quiesce_restored_touch_observed')
for wanted in ('awake_i2c_irqs=3', 'asleep_i2c_irqs=0', 'asleep_touch_irqs=0',
               'asleep_regulator=disabled', 'asleep_supply_gpio=out_lo', 'asleep_status=off',
               'suspend_logged=1', 'resume_logged=1', 'after_touch=observed', 'screen_changes=0'):
    assert wanted in lines, (wanted, lines)
run('touch_quiesce_restored_touch_unconfirmed', contact=False)
# Real i.MX7D hardware: touch_1v8 has SION=0 so GPIO_PSR always reads out_lo.
lines_sion0 = run('touch_quiesce_restored_touch_observed', 'gpio_psr_always_lo')
assert 'supply_gpio=out_lo' in lines_sion0 and 'asleep_supply_gpio=out_lo' in lines_sion0, lines_sion0
# sleep_status reads off in both of these; the driver log, supply and bus decide.
run('touch_sleep_not_confirmed_by_driver', 'sleep_logged_failure')
run('touch_not_powered_down', 'power_stays_on')
run('touch_bus_not_quiet', 'bus_leaks')
run('touch_irq_thread_busy', 'thread_busy')
run('touch_test_disturbed_by_system_sleep', 'system_suspend')
run('touch_test_disturbed_by_system_sleep', 'screen_blank')
run('touch_test_disturbed_by_system_sleep', 'screen_unblank')
run('touch_input_changed', 'reader_changes')
run('touch_wake_failed_restart_required', 'wake_fails')
# Refusals write nothing to the driver.
run('refused_kit_unverified', commands=(), damaged=True)
run('refused_other_touch_users', commands=(), other_user=True)
run('refused_no_awake_bus_activity', 'no_awake_activity', commands=())
run('refused_touch_not_awake', commands=(), asleep_at_start=True)
print('KOA2 touch-only sleep: bus silence against a positive control, supply and GPIO '
      'checked apart from sleep_status, every failure woken and reported')
