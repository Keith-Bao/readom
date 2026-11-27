#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run ports/koa2/stage-test/exec-dryrun.sh against a model Kindle.

Every tool the script calls that changes state is a shell function here:
initctl (x and lab126_gui), insmod/rmmod (rewriting /proc/modules and the fb0
node), the touch/CPU1 sysfs writes (applied before each cat, as in
test_koa2_full_quiesce_dryrun.py), and koa2-stage-client, which prints the
exec report. Checked: the success path restores everything in reverse, each
failing step rolls back what ran before it, the module is handed the claim
the script counted, and nothing reports a jump as available.
"""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/exec-dryrun.sh'
TOUCH = 'sys/bus/i2c/devices/1-0024'
EPDC_KO = 'lib/modules/4.1.15-lab126/kernel/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.ko'
MODULES = ('mxc_epdc_v2_fb 1 3 - Live 0\ng_mass_storage 1 0 - Live 0\n'
           'cyttsp5_i2c 1 0 - Live 0')
# The client stand-in: records the claim, and prints the report the module
# would, with the quiesce verdict following the rootfs bit the way
# koa2_quiesce_verify does.
CLIENT = '''#!/bin/sh
printf '%s\n' "$KOA2_EXEC_CLAIM" > "$KOA2_TEST_ROOT/claim.txt"
echo client >> "$KOA2_TEST_ROOT/ops.log"
case " $* " in *" --exec-dryrun "*) ;; *) exit 2 ;; esac
ro=${KOA2_EXEC_CLAIM##* }
quiesce=0; [ "$ro" = 1 ] || quiesce=-4
params=0; [ "$KOA2_TEST_FAULT" = not_armed ] && params=-4
echo "state=2 plan_valid=1 execute_available=0"
echo "exec_online_cpus=1 exec_quiesce=$quiesce exec_copylist=0 exec_copylist_entries=99"
echo "exec_expected_pages=90 exec_params=$params exec_trampoline_bytes=596"
echo "exec_sctlr=0x10c5387d exec_ttbcr=0x00000000 exec_mmu_on=1 exec_caches_on=1 exec_irq_fiq_masked=1"
echo "exec_watchdog_step=deferred exec_execute_available=0 exec_stopped_before_caches=1"
[ "$params" = 0 ] || exit 3
[ "$quiesce" = 0 ] || exit 3
exit 0
'''
MOCK = '''uname() {{
    case "$1" in
        -r) echo 4.1.15-lab126 ;;
        -v) echo '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ;;
    esac
}}
sync() {{ :; }}
sleep() {{ :; }}
dmesg() {{ :; }}
fault='{fault}'
initctl() {{
    verb=$1; shift
    svc=
    for a in "$@"; do case "$a" in --no-wait) ;; *) svc=$a ;; esac; done
    case "$verb" in
        status) echo "$svc $(command cat "{r}/svc.$svc" 2>/dev/null || echo 'stop/waiting')" ;;
        stop)
            echo "stop $svc" >> "{r}/ops.log"
            echo 'stop/waiting' > "{r}/svc.$svc"
            if [ "$svc" = x ]; then
                if [ "$fault" != epdc_users_stuck ]; then
                    sed 's/^mxc_epdc_v2_fb 1 3 /mxc_epdc_v2_fb 1 0 /' "{r}/proc/modules" > "{r}/proc/modules.n"
                    mv "{r}/proc/modules.n" "{r}/proc/modules"
                    rm -f "{r}/proc/111/fd/3"
                fi
                [ "$fault" = x_stop_fails ] && return 1
            fi ;;
        start)
            echo "start $svc" >> "{r}/ops.log"
            echo 'start/running' > "{r}/svc.$svc"
            if [ "$svc" = x ] && [ "$fault" != x_start_fails ]; then
                mkdir -p "{r}/sys/class/graphics/fb0" "{r}/proc/111/fd"
                echo mxc_epdc_v2_fb > "{r}/sys/class/graphics/fb0/name"
                ln -sf "{r}/dev/fb0" "{r}/proc/111/fd/3"
                echo 1 > "{r}/epdc.users"
            fi
            [ "$svc" = x ] && [ "$fault" = x_start_fails ] && return 1 ;;
    esac
    return 0
}}
insmod() {{
    ko=$1
    echo "insmod $ko" >> "{r}/ops.log"
    case "$ko" in
        *mxc_epdc_v2_fb.ko)
            mkdir -p "{r}/sys/class/graphics/fb0"
            printf '%s\n' "$(command cat "{r}/proc/modules")" | grep -q '^mxc_epdc_v2_fb ' ||
                printf 'mxc_epdc_v2_fb 1 1 - Live 0\n%s' "$(command cat "{r}/proc/modules")" > "{r}/proc/modules" ;;
        *koa2_handoff_exec.ko)
            [ "$fault" = exec_insmod_fails ] && return 1
            printf 'koa2_handoff_exec 1 0 - Live 0\n%s' "$(command cat "{r}/proc/modules")" > "{r}/proc/modules"
            mkdir -p "{r}/sys/class/misc/koa2-handoff-exec"
            echo '10:60' > "{r}/sys/class/misc/koa2-handoff-exec/dev" ;;
    esac
    return 0
}}
rmmod() {{
    m=$1
    echo "rmmod $m" >> "{r}/ops.log"
    grep -v "^$m " "{r}/proc/modules" > "{r}/proc/modules.new" || true
    mv "{r}/proc/modules.new" "{r}/proc/modules"
    [ "$m" = mxc_epdc_v2_fb ] && rm -rf "{r}/sys/class/graphics/fb0" "{r}/epdc.users"
    return 0
}}
mknod() {{ : ; }}
model() {{
    debug=$(command cat "{r}/{TOUCH}/drv_debug" 2>/dev/null)
    if [ -n "$debug" ]; then
        echo "drv_debug $debug" >> "{r}/ops.log"
        : > "{r}/{TOUCH}/drv_debug"
        if [ "$debug" = 4 ]; then
            echo off > "{r}/{TOUCH}/sleep_status"
            [ "$fault" = power_stays_on ] || echo disabled > "{r}/reg.state"
        elif [ "$debug" = 5 ] && [ "$fault" != wake_fails ]; then
            echo on > "{r}/{TOUCH}/sleep_status"; echo enabled > "{r}/reg.state"
        fi
    fi
    cpu1=$(command cat "{r}/sys/devices/system/cpu/cpu1/online")
    last=$(command cat "{r}/cpu1.last" 2>/dev/null)
    if [ "$cpu1" != "$last" ]; then
        echo "cpu1 $cpu1" >> "{r}/ops.log"
        [ "$cpu1" = 0 ] && [ "$fault" = cpu1_stays_online ] && cpu1=1
        [ "$cpu1" = 1 ] && [ "$last" = 0 ] && [ "$fault" = cpu1_restore_fails ] && cpu1=0
        echo "$cpu1" > "{r}/sys/devices/system/cpu/cpu1/online"; echo "$cpu1" > "{r}/cpu1.last"
        if [ "$cpu1" = 1 ]; then echo 0-1; else echo 0; fi > "{r}/sys/devices/system/cpu/online"
    fi
}}
cat() {{ model; command cat "$@"; }}
'''


def run(expected, fault='', overrides=None, damaged=False):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        files = {
            'proc/modules': MODULES, 'proc/uptime': '100.00 180.00',
            'sys/class/udc/ci_hdrc.0/state': 'not attached',
            TOUCH + '/drv_debug': '', TOUCH + '/sleep_status': 'on',
            'reg.state': 'enabled',
            'sys/class/regulator/regulator.7/name': 'MAX77796-LSW1',
            'sys/devices/system/cpu/online': '0-1',
            'sys/devices/system/cpu/cpu1/online': '1', 'cpu1.last': '1',
            'sys/module/mxc_epdc_v2_fb/parameters/waveform': 'wf.wrf.gz',
            'epdc.users': '3', 'svc.x': 'start/running',
            'proc/mounts': 'root / ext4 ro,relatime 0 0\n'
                           '/dev/mmcblk0p4 /mnt/base-us vfat rw,noatime 0 0',
            EPDC_KO: 'ko',
        }
        files.update(overrides or {})
        for name, value in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value + '\n')
        (root / TOUCH / 'driver').mkdir(parents=True)
        (root / 'sys/class/graphics/fb0').mkdir(parents=True)
        (root / 'sys/class/graphics/fb0/name').write_text('mxc_epdc_v2_fb\n')
        (root / 'mnt/us/documents').mkdir(parents=True)
        (root / 'dev/fb0').parent.mkdir(parents=True, exist_ok=True)
        (root / 'dev/fb0').touch()
        (root / 'proc/111/fd').mkdir(parents=True)
        (root / 'proc/111/comm').write_text('Xorg' + chr(10))
        (root / 'proc/111/fd/3').symlink_to(root / 'dev/fb0')
        (root / 'ops.log').write_text('')
        r = str(root)
        # The regulator state the touch_power helper reads is a symlink onto the
        # single reg.state file, so a write through either path is seen.
        (root / 'sys/class/regulator/regulator.7/state').symlink_to(root / 'reg.state')
        # initctl status for lab126_gui starts idle.
        (root / 'svc.lab126_gui').write_text('stop/waiting\n')
        client = root / 'koa2-stage-client'
        client.write_text(CLIENT)
        client.chmod(0o755)
        (root / 'zImage').write_text('z'); (root / 'initramfs.cpio.gz').write_text('i')
        mock = MOCK.format(r=r, fault=fault, TOUCH=TOUCH)
        text = re.sub(r'/mnt/us|/proc/|/sys/|/lib/modules|/dev/fb0',
                      lambda m: r + m[0], SCRIPT.read_text())
        # The script finds the client, kit and waveform under $HERE; point HERE
        # at the model root, and the EPDC .ko path at the one we created.
        script = root / 'exec-dryrun.sh'
        script.write_text(mock + text)
        digest = hashlib.sha256(script.read_bytes()).hexdigest()
        if damaged:
            digest = '0' * 8 + digest[8:]
        lines_for_sums = [f'{digest}  exec-dryrun.sh']
        for extra in ('koa2-stage-client', 'zImage', 'initramfs.cpio.gz'):
            d = hashlib.sha256((root / extra).read_bytes()).hexdigest()
            lines_for_sums.append(f'{d}  {extra}')
        (root / 'SHA256SUMS').write_text('\n'.join(lines_for_sums) + '\n')
        env = dict(os.environ, KOA2_TEST_ROOT=r, KOA2_TEST_FAULT=fault)
        subprocess.run(['sh', str(script)], check=True, timeout=30, env=env)
        base = root / 'mnt/us/koa2-exec-dryrun'
        report, = base.glob('*/report.txt')
        lines = report.read_text().splitlines()
        assert lines[-1] == 'result=' + expected, (expected, lines)
        assert not (base / '.running').exists(), expected
        claim = (root / 'claim.txt').read_text().strip() if (root / 'claim.txt').exists() else None
        return lines, (root / 'ops.log').read_text().splitlines(), claim


def stages(lines):
    return [l.split()[0][len('stage='):] for l in lines if l.startswith('stage=')]


def field(lines, key):
    """The value of key=VALUE, wherever it sits on a line (the client prints
    several per line)."""
    for l in lines:
        for token in l.split():
            if token.startswith(key + '='):
                return token.split('=', 1)[1]
    return None


def test_success_restores_in_reverse():
    lines, ops, claim = run('exec_dryrun_restored')
    # The claim the script counted and handed the module: all four quiesce
    # drivers still loaded on entry (epdc+usb+touch=3, epdc reloaded makes the
    # count the module sees), touch and usb present, rootfs read-only.
    assert claim is not None and claim.split()[-1] == '1', claim
    seen = stages(lines)
    # Forward: stop x, rmmod epdc, usb verify, touch sleep, cpu1 offline, sync,
    # insmod exec, dry-run. Then reverse: rmmod exec, cpu1, touch, insmod epdc, x.
    for s in ('stop_x', 'rmmod_epdc', 'usb_verify_idle', 'touch_sleep', 'cpu1_offline',
              'sync', 'insmod_exec', 'exec_dryrun',
              'restore_rmmod_exec', 'restore_cpu1', 'restore_touch', 'restore_insmod_epdc',
              'restore_x', 'done'):
        assert s in seen, (s, seen)
    assert seen.index('restore_cpu1') < seen.index('restore_insmod_epdc') < seen.index('restore_x')
    assert field(lines, 'step_epdc') == 'unloaded'
    assert field(lines, 'step_usb') == 'quiesced_idle'
    assert field(lines, 'step_cpu1') == 'offline'
    assert field(lines, 'exec_stopped_before_caches') == '1'
    assert field(lines, 'exec_execute_available') == '0'
    assert field(lines, 'fb_restored') == '1'
    assert 'insmod ' + EPDC_KO.rsplit('/', 1)[-1] not in ops  # reloaded by full path
    assert any(o.startswith('insmod ') and o.endswith('mxc_epdc_v2_fb.ko') for o in ops)
    assert any(o == 'rmmod koa2_handoff_exec' for o in ops)


def test_rootfs_writable_reported_not_fatal():
    # / mounted rw: the module's quiesce verdict refuses, but the dry-run still
    # armed and stopped before the caches, and everything is put back.
    lines, ops, claim = run('exec_dryrun_restored_not_quiesced',
                            overrides={'proc/mounts': 'root / ext4 rw,relatime 0 0'})
    assert claim.split()[-1] == '0', claim
    assert field(lines, 'rootfs_readonly') == '0'
    assert field(lines, 'exec_stopped_before_caches') == '1'
    assert 'restore_x' in stages(lines)


def test_each_precondition_refuses():
    run('refused_wifi_loaded', overrides={'proc/modules': MODULES + '\nbcmdhd 1 0 - Live 0'})
    run('refused_stage_module_loaded', overrides={'proc/modules': MODULES + '\nkoa2_handoff_stage 1 0 - Live 0'})
    run('refused_epdc_not_loaded', overrides={'proc/modules': 'g_mass_storage 1 0 - Live 0\ncyttsp5_i2c 1 0 - Live 0'})
    run('refused_usb_host_connected', overrides={'sys/class/udc/ci_hdrc.0/state': 'configured'})
    run('refused_touch_not_awake', overrides={TOUCH + '/sleep_status': 'off'})
    run('refused_cpu1_not_online', overrides={'sys/devices/system/cpu/online': '0',
                                              'sys/devices/system/cpu/cpu1/online': '0', 'cpu1.last': '0'})
    run('refused_kit_unverified', damaged=True)


def test_epdc_stuck_rolls_back_just_x():
    # The three fb0 users never release after stop x: no rmmod, and x is
    # restarted. Nothing else ran, so nothing else to undo.
    lines, ops, _ = run('step_epdc_users_not_released', fault='epdc_users_stuck')
    seen = stages(lines)
    assert 'rmmod_epdc' not in seen and 'usb_verify_idle' not in seen
    assert 'restore_x' in seen and 'restore_insmod_epdc' not in seen
    assert 'rmmod mxc_epdc_v2_fb' not in ops


def test_cpu1_offline_failure_rolls_back_touch_and_epdc():
    lines, ops, _ = run('step_cpu1_offline_failed', fault='cpu1_stays_online')
    seen = stages(lines)
    assert 'restore_touch' in seen and 'restore_insmod_epdc' in seen and 'restore_x' in seen
    assert 'restore_rmmod_exec' not in seen  # the module was never loaded


def test_exec_insmod_refused_rolls_back_all_quiesce():
    lines, ops, _ = run('step_exec_insmod_refused', fault='exec_insmod_fails')
    seen = stages(lines)
    assert 'restore_cpu1' in seen and 'restore_touch' in seen and 'restore_insmod_epdc' in seen
    assert 'restore_rmmod_exec' not in seen


def test_exec_not_armed_still_restores():
    lines, ops, _ = run('step_exec_not_armed', fault='not_armed')
    assert field(lines, 'exec_params') == '-4'
    assert 'restore_rmmod_exec' in stages(lines)


def test_restore_failure_demands_restart():
    lines, ops, _ = run('exec_restore_failed_restart_required', fault='cpu1_restore_fails')
    assert field(lines, 'restore_cpu1') == 'failed'


if __name__ == '__main__':
    test_success_restores_in_reverse()
    test_rootfs_writable_reported_not_fatal()
    test_each_precondition_refuses()
    test_epdc_stuck_rolls_back_just_x()
    test_cpu1_offline_failure_rolls_back_touch_and_epdc()
    test_exec_insmod_refused_rolls_back_all_quiesce()
    test_exec_not_armed_still_restores()
    test_restore_failure_demands_restart()
    print('KOA2 exec dry-run: success restores in reverse, rootfs-rw reported, every '
          'precondition refuses, each step rolls back what ran before it, no jump')
