#!/usr/bin/env python3
"""ports/koa2/stage-test/quiesce-dryrun.sh, against a simulated device.

The script stops every DMA master that can be stopped from userspace and then
puts it all back. The property that matters is the putting back: it runs on a
device the user is holding, and a run that stopped the Wi-Fi and the touch
panel and then failed would leave them stopped.

So this builds a fake /proc, /sys and command set, runs the real script against
them, and checks what it did and in which order. The cases are the ways a real
device can refuse: a module in use, an rmmod that fails, a remount that fails,
a core that will not go down.
"""
import os
import pathlib
import re
import shutil
import stat
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/quiesce-dryrun.sh'
MODULES = ['mxc_epdc_v2_fb 3 0', 'bcmdhd 1 0', 'g_mass_storage 2 0',
           'usb_f_mass_storage 1 2', 'libcomposite 1 2', 'cyttsp5_i2c 1 0',
           'bma2x2 1 0', 'als1_max44009 1 1', 'falcon 1 0']
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def fake_device(work, fail=None, in_use=()):
    """A root with the files the script reads, and stubs for the commands it
    runs. Every stub records what it was asked to do, in order."""
    fail = fail or set()
    root = work / 'root'
    for d in ('proc', 'sys/devices/system/cpu/cpu1', 'bin', 'mnt/us/documents'):
        (root / d).mkdir(parents=True, exist_ok=True)
    loaded = [m for m in MODULES]
    (root / 'proc/modules').write_text('\n'.join(
        m if m.split()[0] not in in_use else '%s %s 1 other,' % tuple(m.split()[:2])
        for m in loaded) + '\n')
    (root / 'proc/version').write_text('Linux version 4.1.15-lab126\n')
    (root / 'proc/meminfo').write_text('MemFree:  6252 kB\nMemAvailable: 215952 kB\n')
    (root / 'proc/mounts').write_text('/dev/mmcblk2p5 / ext4 rw,relatime 0 0\n'
                                      '/dev/mmcblk2p7 /mnt/us vfat rw 0 0\n')
    (root / 'sys/devices/system/cpu/online').write_text('0-1\n')
    (root / 'sys/devices/system/cpu/cpu1/online').write_text('1\n')
    actions = root / 'actions.log'
    actions.write_text('')
    for name in ('rmmod', 'modprobe', 'mount', 'sync', 'uname'):
        body = ['#!/bin/sh', 'echo "%s $*" >> %s' % (name, actions)]
        if name == 'uname':
            body = ['#!/bin/sh', 'case "$1" in -r) echo 4.1.15-lab126 ;; *) echo "Linux kindle" ;; esac']
        elif name == 'rmmod':
            body += [
                'if [ "$1" = %s ]; then exit 1; fi' % (next(iter(fail & {m.split()[0] for m in MODULES}), '__none__')),
                'grep -v "^$1 " %s > %s.new && mv %s.new %s' % ((root / 'proc/modules',) * 4),
            ]
        elif name == 'mount':
            if 'remount' in fail:
                body += ['exit 1']
            else:
                body += ['case "$*" in *remount,ro*) sed -i "s#/ ext4 rw#/ ext4 ro#" %s ;;'
                         ' *remount,rw*) sed -i "s#/ ext4 ro#/ ext4 rw#" %s ;; esac' %
                         (root / 'proc/mounts', root / 'proc/mounts')]
        elif name == 'modprobe':
            body += ['echo "$1 1 0" >> %s' % (root / 'proc/modules')]
        path = root / 'bin' / name
        path.write_text('\n'.join(body) + '\n')
        path.chmod(0o755)
    return root, actions


def run(root, script):
    """Runs the script with its paths pointed into the fake root."""
    text = script.read_text()
    text = text.replace('/mnt/us', str(root / 'mnt/us'))
    text = text.replace('/proc/', str(root / 'proc') + '/')
    text = text.replace('/sys/devices', str(root / 'sys/devices'))
    # busybox resolves uname, rmmod, modprobe, mount and sync to its own
    # applets regardless of PATH, so the calls are pointed at the stubs.
    for name in ('uname', 'rmmod', 'modprobe', 'mount', 'sync'):
        text = re.sub(r'(?<![\w/])%s(?= |$)' % name, str(root / 'bin' / name), text, flags=re.M)
    local = root / 'quiesce.sh'
    local.write_text(text)
    env = dict(os.environ, PATH='%s:%s' % (root / 'bin', os.environ['PATH']))
    return subprocess.run(['busybox', 'sh', str(local)], capture_output=True, text=True, env=env, timeout=120)


def report_of(root):
    runs = sorted((root / 'mnt/us/koa2-quiesce-dryrun').glob('*/report.txt'))
    return runs[-1].read_text() if runs else ''


def case_everything_works():
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        root, actions = fake_device(work)
        p = run(root, SCRIPT)
        text = report_of(root)
        done = actions.read_text()
        check('result=quiesced_and_restored' in text, 'clean run: %r' % text[-400:])
        # The four masters were removed, and the EPDC went first.
        removed = re.findall(r'removed=(\S+)', text)
        check(removed[:1] == ['mxc_epdc_v2_fb'], 'the EPDC should be stopped first, got %s' % removed[:1])
        for m in ('bcmdhd', 'g_mass_storage', 'cyttsp5_i2c'):
            check(m in removed, '%s was not removed' % m)
        check('falcon' not in removed, 'falcon (eMMC) must never be removed')
        # Everything was put back, and the rootfs went read-write before the
        # modules were reloaded.
        reloaded = re.findall(r'reloaded=(\S+)', text)
        check(set(reloaded) == set(removed), 'put back %s of %s' % (sorted(reloaded), sorted(removed)))
        check(reloaded == list(reversed(removed)), 'should be put back in reverse: %s' % reloaded)
        rw_at = done.find('mount -o remount,rw')
        first_modprobe = done.find('modprobe')
        check(0 <= rw_at < first_modprobe, 'the rootfs must go read-write before any modprobe')
        check('cpu1=online' in text, 'cpu1 was not put back online')
        check(text.index('cpu1_down_seconds') < text.index('[verify]'), 'cpu1 goes down before the check')
        check('online_fields=1 ' in text, 'the check must see one core: %r' %
              (re.search(r'online_fields=\S+', text).group(0) if re.search(r'online_fields=\S+', text) else ''))
        # The verification the module would do, done the same way here.
        check('drivers_present=0 rootfs_readonly=1' in text, 'verify line: %r' %
              (re.search(r'online_fields=.*', text).group(0) if re.search(r'online_fields=.*', text) else ''))
        check(p.returncode == 0, 'exit %d' % p.returncode)


def case_refusals():
    # A module in use is left alone, and the run says so rather than forcing it.
    with tempfile.TemporaryDirectory() as tmp:
        root, actions = fake_device(pathlib.Path(tmp), in_use={'bcmdhd'})
        run(root, SCRIPT)
        text = report_of(root)
        check('IN_USE=bcmdhd' in text, 'a module in use should be reported: %r' % text[-300:])
        check('result=incomplete_see_above' in text, 'an incomplete run must say so')
        check('reloaded=mxc_epdc_v2_fb' in text, 'what was removed must still be put back')
    # An rmmod that fails: the ones already removed are still restored.
    with tempfile.TemporaryDirectory() as tmp:
        root, actions = fake_device(pathlib.Path(tmp), fail={'bcmdhd'})
        run(root, SCRIPT)
        text = report_of(root)
        check('REMOVE_FAILED=bcmdhd' in text, 'a failed rmmod should be reported')
        check('reloaded=mxc_epdc_v2_fb' in text, 'the EPDC must be put back after a later failure')
        check('result=incomplete_see_above' in text, 'should not claim success')
    # A remount that fails: nothing is left read-only and the run refuses.
    with tempfile.TemporaryDirectory() as tmp:
        root, actions = fake_device(pathlib.Path(tmp), fail={'remount'})
        run(root, SCRIPT)
        text = report_of(root)
        check('ROOTFS_REMOUNT_FAILED' in text, 'a failed remount should be reported')
        check('result=incomplete_see_above' in text, 'should not claim success')
        check('rootfs_readonly=0' in text, 'the check must see the rootfs is still writable')


def case_wrong_kernel():
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        root, actions = fake_device(work)
        uname = root / 'bin/uname'
        uname.write_text('#!/bin/sh\ncase "$1" in -r) echo 4.9.0-other ;; *) echo Linux ;; esac\n')
        uname.chmod(0o755)
        run(root, SCRIPT)
        text = report_of(root)
        check('result=refused_unknown_kernel' in text, 'another kernel must be refused: %r' % text[-200:])
        check('removed=' not in text, 'nothing may be removed on a kernel it does not know')


if not shutil.which('busybox'):
    print('SKIP needs busybox')
    sys.exit(0)
case_everything_works()
case_refusals()
case_wrong_kernel()
for f in failures:
    print('FAIL', f)
if failures:
    sys.exit(1)
print('quiesce dry run: the four masters stop in order and every one is put back, '
      'the rootfs goes read-write before any reload, and a module in use, a failed '
      'rmmod, a failed remount and another kernel are all refused')
