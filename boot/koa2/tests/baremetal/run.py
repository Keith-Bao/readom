#!/usr/bin/env python3
"""Runs boot/koa2/jump.S on an emulated Cortex-A7, with the MMU off.

The host tests reach everything about the handoff except the part that cannot
be written in C: clearing the MMU bit and carrying on from the same physical
address, reading the parameter block through the PC, and the registers the new
kernel is entered with. This builds a bare-metal image around the real
trampoline and the real copy-list code, boots it, and reads the result.

A pass is the stand-in kernel printing KO!, which it does only when it was
entered with r0 = 0, r1 = the machine number and r2 pointing at the FDT it was
given. A refusal is read out of the trampoline's own parameter block through
the QEMU monitor, together with the destination page, because what matters
about a refusal is that it did not leave a half-copied kernel behind.
"""
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
NEEDS = ['arm-linux-gnueabi-gcc', 'qemu-system-arm', 'make']
KJ_COPIED, KJ_STATUS = 36, 40
# case: the status expected, and whether pages may already have been copied
# when it is reached. Once the MMU is off there is no undo, so a refusal that
# can only happen part way through a list leaves what it had copied; the
# refusals that must leave nothing are the ones reachable on the first entry.
# The caller closes this by setting the budget to the audited entry count, so a
# list koa2_copylist_audit accepted cannot reach the budget at all.
CASES = {
    'pass': (0xa3, True),
    'caches_on': (0xe1, False),
    'bad_link': (0xe5, False),
    'bad_source': (0xe6, False),
    'short_count': (0xeb, True),
    'budget': (0xe2, True),
}


def monitor(path, command, wait=0.6):
    s = socket.socket(socket.AF_UNIX)
    s.connect(path)
    s.settimeout(2)
    s.sendall((command + '\n').encode())
    time.sleep(wait)
    data = b''
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    s.close()
    return data.decode(errors='replace')


def words(dump):
    """The words of an `xp` dump, in order."""
    out = []
    for line in dump.splitlines():
        if ':' not in line:
            continue
        for field in line.split(':', 1)[1].split():
            if re.fullmatch(r'0x[0-9a-f]{8}', field):
                out.append(int(field, 16))
    return out


def run_case(case, work):
    subprocess.run(['make', '-s', 'clean'], cwd=HERE, check=True)
    build = subprocess.run(['make', '-s', 'CASE=' + case], cwd=HERE, capture_output=True, text=True)
    if build.returncode:
        return f'build failed\n{build.stdout}{build.stderr}'
    sock = str(work / ('mon-' + case))
    log = work / ('out-' + case)
    with open(log, 'wb') as out:
        qemu = subprocess.Popen(['qemu-system-arm', '-M', 'virt', '-cpu', 'cortex-a7', '-m', '128M',
                                 '-display', 'none', '-no-reboot', '-serial', 'file:' + str(log) + '.serial',
                                 '-monitor', 'unix:' + sock + ',server,nowait',
                                 '-kernel', str(HERE / 'jump-test.elf')], stdout=out, stderr=out)
    try:
        for _ in range(100):
            if pathlib.Path(sock).exists():
                break
            time.sleep(0.1)
        time.sleep(2.5)
        serial = pathlib.Path(str(log) + '.serial').read_text(errors='replace')
        found = re.search(r'params (0x[0-9a-f]+)', serial)
        dest = re.search(r'dest (0x[0-9a-f]+)', serial)
        if not found or not dest:
            return f'no parameter address in output\n{serial[-1500:]}'
        params = int(found.group(1), 16)
        status, copied_may = CASES[case]
        got = words(monitor(sock, f'xp /2wx {params + KJ_COPIED:#x}'))
        if len(got) < 2:
            return f'cannot read the parameter block\n{serial[-1500:]}'
        copied, got_status = got[0], got[1]
        untouched = words(monitor(sock, f'xp /1wx {int(dest.group(1), 16):#x}'))
        if got_status != status:
            return f'status {got_status:#x}, expected {status:#x} (copied {copied})\n{serial[-1500:]}'
        if case == 'pass':
            if 'KO!' not in serial:
                return f'the stand-in kernel did not confirm its registers\n{serial[-1500:]}'
        else:
            if 'KO!' in serial:
                return 'refused but the kernel was entered anyway'
            if not copied_may and copied:
                return f'refused after copying {copied} pages'
            if not copied_may and untouched and untouched[0]:
                return f'refused but the destination holds {untouched[0]:#x}'
        return None
    finally:
        qemu.kill()
        qemu.wait(timeout=10)
        subprocess.run(['make', '-s', 'clean'], cwd=HERE, check=True)


def main():
    missing = [t for t in NEEDS if not shutil.which(t)]
    if missing:
        print('SKIP needs ' + ', '.join(missing))
        return 0
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for case in CASES:
            why = run_case(case, work)
            if why:
                failures.append(f'{case}: {why}')
    for f in failures:
        print('FAIL', f)
    if failures:
        return 1
    print('jump.S on an emulated Cortex-A7: the entry contract and %d refusals, '
          'none of which copied a page it should not' % (len(CASES) - 1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
