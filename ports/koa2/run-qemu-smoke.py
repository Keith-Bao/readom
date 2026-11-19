#!/usr/bin/env python3
"""Boot the KOA2 headless-smoke kernel and smoke initramfs on QEMU mcimx7d-sabre.

QEMU only, never a Kindle. Passes when the init reports PASS, releases the
watchdog, and the emulated i.MX watchdog resets the machine (QEMU exits because
of -no-reboot) before the time limit, with no kernel WARNING/BUG/Oops/panic."""
import argparse
import os
import subprocess
import time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--kernel', type=Path, default=Path.home() / 'artifacts/headless-smoke-kernel/zImage')
p.add_argument('--dtb', type=Path, default=Path.home() / 'readom-work/out-headless-smoke/arch/arm/boot/dts/imx7d-sdb.dtb')
p.add_argument('--initrd', type=Path, default=Path.home() / 'artifacts/smoke-initramfs/initramfs.cpio.gz')
p.add_argument('--log', type=Path, default=Path.home() / 'artifacts/smoke-initramfs/qemu.log')
p.add_argument('--limit', type=int, default=150)
a = p.parse_args()
# QEMU's mcimx7d-sabre does not back the OCRAM_S (0x180000) or DDR low-power OCRAM
# (0x900000) the stock low-power code clears at map_io time; both abort. Drop that node from a QEMU-only copy of the DTB.
# The Kindle runtime DTB is never edited here.
qdts = subprocess.run(['dtc', '-q', '-I', 'dtb', '-O', 'dts', str(a.dtb)], check=True,
                      stdout=subprocess.PIPE, text=True).stdout
for node in ('sram@00180000 {', 'sram@00900000 {'):
    start = qdts.find(node)
    if start < 0 or qdts.count(node) != 1:
        raise SystemExit(f'Unexpected DTB: {node} not found exactly once')
    end = qdts.index('};', start) + 2
    qdts = qdts[:start] + qdts[end:]
# The DDR controller (0x307a0000) is not modelled either; imx_ddrc_probe() aborts.
ddrc = 'ddrc@307a0000 {'
if qdts.count(ddrc) == 1:
    i = qdts.index(ddrc)
    qdts = qdts[:i] + ddrc + ' status = "disabled";' + qdts[i + len(ddrc):]
qemu_dtb = a.log.with_name('qemu-only.dtb')
subprocess.run(['dtc', '-q', '-I', 'dts', '-O', 'dtb', '-o', str(qemu_dtb)], input=qdts,
               check=True, text=True)
command = ['qemu-system-arm', '-M', 'mcimx7d-sabre', '-smp', '2', '-m', '512M',
           '-display', 'none', '-serial', 'stdio', '-monitor', 'none', '-no-reboot', '-nic', 'none',
           '-kernel', str(a.kernel), '-dtb', str(qemu_dtb), '-initrd', str(a.initrd),
           '-append', # maxcpus=1: QEMU does not emulate the GPCv2 core-1 power-up handshake, so
           # imx_boot_secondary() spins forever. The device itself brings up CPU1.
           'console=ttymxc0,115200 rdinit=/init maxcpus=1 koa2_smoke_hold=5 koa2_smoke_wdt=4']
started = time.monotonic()
try:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=a.limit, env=dict(os.environ, QEMU_AUDIO_DRV='none'))
    output, code, timed_out = result.stdout, result.returncode, False
except subprocess.TimeoutExpired as exc:
    output, code, timed_out = exc.stdout or b'', None, True
elapsed = time.monotonic() - started
a.log.write_bytes(output)
text = output.decode(errors='replace')
bad = [w for w in ('WARNING:', 'BUG:', 'Oops:', 'Kernel panic', 'KOA2_SMOKE FAIL') if w in text]
passed = 'KOA2_SMOKE PASS released_watchdog' in text and not timed_out and code == 0 and not bad
print(f'elapsed={elapsed:.1f}s timed_out={timed_out} exit={code} bad={bad}')
for line in text.splitlines():
    if 'KOA2_SMOKE' in line:
        print(line.strip())
if not passed:
    print(text[-6000:])
    raise SystemExit('QEMU smoke failed; see ' + str(a.log))
print('QEMU i.MX7D smoke passed: init ran, released the watchdog, watchdog reset ended QEMU. Not Kindle hardware validation.')
