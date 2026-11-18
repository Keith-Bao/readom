#!/usr/bin/env python3
"""Bounded QEMU-only Binder smoke; never targets Kindle hardware."""
import argparse
import os
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--artifacts', type=Path, default=Path.home() / 'artifacts/qemu-binder')
a = p.parse_args()
command = ['qemu-system-arm', '-M', 'vexpress-a9', '-cpu', 'cortex-a9', '-smp', '2',
           '-m', '512M', '-display', 'none', '-serial', 'stdio', '-monitor', 'none',
           '-no-reboot', '-nic', 'none', '-kernel', str(a.artifacts / 'zImage'),
           '-dtb', str(a.artifacts / 'vexpress-v2p-ca9.dtb'),
           '-initrd', str(a.artifacts / 'initramfs.cpio.gz'),
           '-append', 'console=ttyAMA0 rdinit=/init panic=5 koa2_binder_smoke=1']
env = dict(os.environ, QEMU_AUDIO_DRV='none')
try:
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=60, env=env)
    output = result.stdout
except subprocess.TimeoutExpired as exc:
    output = exc.stdout or b''
    (a.artifacts / 'runtime.log').write_bytes(output)
    raise SystemExit('QEMU smoke timed out; inspect runtime.log')
(a.artifacts / 'runtime.log').write_bytes(output)
bad = (b'KOA2_BINDER_SMOKE_FAIL', b'WARNING:', b'BUG:', b'Oops:', b'Kernel panic')
if result.returncode or b'KOA2_BINDER_SMOKE_PASS failures=0' not in output or any(word in output for word in bad):
    print(output.decode(errors='replace')[-8000:])
    raise SystemExit('QEMU smoke failed; inspect runtime.log')
print('QEMU Binder protocol/mmap, contexts, one-way, synchronous FD and scatter-gather smoke passed; not Kindle hardware validation.')
