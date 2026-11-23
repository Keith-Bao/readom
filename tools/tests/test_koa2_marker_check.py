#!/usr/bin/env python3
"""ports/koa2/stage-test/marker-test.sh, against a simulated device.

The half that matters here is `check`, run after a restart. It reads
/proc/printkbuf -- the previous boot's log -- and reports whether the marker is
in it. What makes that reading delicate is the meaning of silence: the runbook
tells whoever is holding the Kindle that `marker_absent` may mean the reset does
not preserve DRAM, which would make section two of recovery.md wrong. So an
empty result must not be reachable by any route other than a marker that really
is not there.

The first version of the script piped the buffer through `strings`. That applet
is not in every busybox build, and nothing in this port's device evidence shows
it on this Kindle; the preflight collector read the same buffer with `tr`. A
missing applet writes nothing and exits non-zero, and the script would have
called that `marker_absent` -- the alarming answer, from a tool that never ran.

So the extraction is `tr`, and the read has to prove itself: every boot of this
kernel leaves hundreds of bracketed timestamps in the buffer, and finding none
of them is reported as `refused_buffer_unreadable` instead.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/marker-test.sh'
MARKER = 'KOA2_HANDOFF v1 attempt=1 uptime=42s entry=0x80d00000 fdt=0x81400000'
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def a_real_buffer(with_marker, lines=600):
    """What /proc/printkbuf hands back: NUL-separated kernel log records with
    the tail zero-filled, like the 262144-byte capture taken from the device on
    2026-11-19."""
    records = ['[ %4d.%06d] something the kernel said %d'
               % (n // 10, n * 991 % 1000000, n) for n in range(lines)]
    if with_marker:
        records.append(MARKER)
    blob = chr(0).join(records).encode()
    return blob + bytes(262144 - len(blob))


def fake_device(work, buffer_bytes, no_printkbuf=False, break_tr=False):
    root = work / 'root'
    for d in ('proc', 'bin', 'mnt/us/documents'):
        (root / d).mkdir(parents=True, exist_ok=True)
    (root / 'proc/version').write_text('Linux version 4.1.15-lab126\n')
    if not no_printkbuf:
        (root / 'proc/printkbuf').write_bytes(buffer_bytes)
    uname = root / 'bin/uname'
    uname.write_text('#!/bin/sh\ncase "$1" in -r) echo 4.1.15-lab126 ;;'
                     ' *) echo "Linux kindle" ;; esac\n')
    uname.chmod(0o755)
    if break_tr:
        # An applet this busybox does not have: it writes nothing and fails,
        # which is the trap the strings version fell into.
        tr = root / 'bin/tr'
        tr.write_text('#!/bin/sh\necho "tr: applet not found" >&2\nexit 127\n')
        tr.chmod(0o755)
    return root


def run(root, break_tr=False):
    text = SCRIPT.read_text()
    text = text.replace('/mnt/us', str(root / 'mnt/us'))
    text = text.replace('/proc/printkbuf', str(root / 'proc/printkbuf'))
    text = text.replace('/proc/version', str(root / 'proc/version'))
    # busybox resolves these to its own applets whatever PATH says, so the
    # calls are pointed straight at the stubs.
    for name in (['uname', 'tr'] if break_tr else ['uname']):
        text = re.sub(r'(?<![\w/])%s(?= )' % name, str(root / 'bin' / name), text, flags=re.M)
    local = root / 'marker.sh'
    local.write_text(text)
    env = dict(os.environ, PATH='%s:%s' % (root / 'bin', os.environ['PATH']))
    return subprocess.run(['busybox', 'sh', str(local), 'check'],
                          capture_output=True, text=True, env=env, timeout=120)


def report_dir_of(root):
    runs = sorted((root / 'mnt/us/koa2-marker-test').glob('*/report.txt'))
    return runs[-1].parent if runs else None


def report_of(root):
    d = report_dir_of(root)
    return (d / 'report.txt').read_text() if d else ''


def case_marker_is_there():
    with tempfile.TemporaryDirectory() as tmp:
        root = fake_device(pathlib.Path(tmp), a_real_buffer(True))
        run(root)
        text = report_of(root)
        check('result=marker_survived' in text,
              'a marker in the buffer must be found: %r' % text[-400:])
        check('markers_found=1' in text, 'exactly the one marker line: %r' % text[-300:])
        check(MARKER in text, 'the marker line itself belongs in the report')
        got = re.search(r'previous_boot_log_lines=(\d+)', text)
        check(got and int(got.group(1)) >= 20,
              'the read must be shown to have worked: %s' % (got and got.group(0)))
        # The device's own log does not leave the device.
        d = report_dir_of(root)
        check(not (d / 'previous-boot.bin').exists(), 'the raw buffer must be deleted')
        check(not (d / 'previous-boot.txt').exists(), 'the extracted log must be deleted')
        kept = sorted(p.name for p in d.iterdir())
        check(kept == ['markers.txt', 'report.txt'],
              'only the marker lines and the report may stay: %s' % kept)
        check(MARKER in (d / 'markers.txt').read_text(), 'markers.txt holds the line')
        check('something the kernel said' not in (d / 'markers.txt').read_text(),
              "the device's own log lines must not be kept")
        check('something the kernel said' not in text, 'nor written into the report')


def case_marker_is_really_absent():
    with tempfile.TemporaryDirectory() as tmp:
        root = fake_device(pathlib.Path(tmp), a_real_buffer(False))
        run(root)
        text = report_of(root)
        check('result=marker_absent' in text,
              'a readable buffer without a marker is absent: %r' % text[-400:])
        check('markers_found=0' in text, 'no marker lines found')


def case_the_read_failed():
    """The case that matters: the extraction produced nothing. That is not an
    absent marker, and calling it one would send the reader at section 8c to the
    wrong conclusion about DRAM surviving the reset."""
    with tempfile.TemporaryDirectory() as tmp:
        root = fake_device(pathlib.Path(tmp), a_real_buffer(True), break_tr=True)
        run(root, break_tr=True)
        text = report_of(root)
        check('result=refused_buffer_unreadable' in text,
              'a failed read must refuse, not report an absent marker: %r' % text[-400:])
        check('result=marker_absent' not in text, 'it must never read as marker_absent')
        check('previous_boot_log_lines=0' in text, 'the report should show the read found nothing')


def case_an_empty_buffer():
    """Nothing but zeros: no log in it, so nothing can be concluded."""
    with tempfile.TemporaryDirectory() as tmp:
        root = fake_device(pathlib.Path(tmp), bytes(262144))
        run(root)
        text = report_of(root)
        check('result=refused_buffer_unreadable' in text,
              'an empty buffer is not an absent marker: %r' % text[-400:])


def case_no_printkbuf():
    with tempfile.TemporaryDirectory() as tmp:
        root = fake_device(pathlib.Path(tmp), b'', no_printkbuf=True)
        run(root)
        text = report_of(root)
        check('result=refused_no_printkbuf' in text,
              'without the file the whole log route is absent: %r' % text[-300:])


def case_wrong_kernel():
    with tempfile.TemporaryDirectory() as tmp:
        root = fake_device(pathlib.Path(tmp), a_real_buffer(True))
        uname = root / 'bin/uname'
        uname.write_text('#!/bin/sh\ncase "$1" in -r) echo 4.9.0-other ;; *) echo Linux ;; esac\n')
        uname.chmod(0o755)
        run(root)
        text = report_of(root)
        check('result=refused_unknown_kernel' in text,
              'another kernel must be refused: %r' % text[-200:])
        check('markers_found' not in text, 'nothing may be read on a kernel it does not know')


def case_it_does_not_use_strings():
    """The applet this port has no evidence for on this device.

    Read from the commands only: the script's own comments say the word, which
    is where the first version of this check found it.
    """
    code = [line.split('#')[0] for line in SCRIPT.read_text().splitlines()]
    check(not re.search(r'(?<![\w-])strings ', chr(10).join(code)),
          'marker-test.sh must not depend on a strings applet')


if not shutil.which('busybox'):
    print('SKIP needs busybox')
    sys.exit(0)
case_marker_is_there()
case_marker_is_really_absent()
case_the_read_failed()
case_an_empty_buffer()
case_no_printkbuf()
case_wrong_kernel()
case_it_does_not_use_strings()
for f in failures:
    print('FAIL', f)
if failures:
    sys.exit(1)
print('marker test check: a marker is found and only its own lines kept, a readable '
      'buffer without one is absent, and a failed read, an empty buffer, a missing '
      'printkbuf and another kernel each refuse instead')
