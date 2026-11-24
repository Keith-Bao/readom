#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host test for KOA2 installer, upgrade, failure refusal, and rollback."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'ports/koa2/installer/koa2-manifest.py'
INSTALL = ROOT / 'ports/koa2/installer/koa2-install.sh'
ROLLBACK = ROOT / 'ports/koa2/installer/koa2-rollback.sh'


def make_pkg(path: Path, version: str, payload: str):
    path.mkdir(parents=True, exist_ok=True)
    (path / 'payload.bin').write_text(payload)
    subprocess.run([sys.executable, str(MANIFEST), 'create', '--dir', str(path), '--version', version], check=True)
    subprocess.run([sys.executable, str(MANIFEST), 'verify', '--dir', str(path)], check=True)


def run_sh(script: Path, base: Path, args=(), release='4.1.15-lab126'):
    wrapper = f'uname() {{ echo "{release}"; }}\n' + script.read_text()
    with tempfile.NamedTemporaryFile('w', suffix='.sh', delete=False) as tf:
        tf.write(wrapper)
        tf_path = Path(tf.name)
    try:
        env = dict(os.environ, KOA2_INSTALL_BASE=str(base))
        out = subprocess.check_output(['sh', str(tf_path), *map(str, args)], env=env, text=True).strip()
        return out.splitlines()[-1]
    finally:
        tf_path.unlink(missing_ok=True)


with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    base = t / 'install'
    v1, v2, bad = t / 'v1', t / 'v2', t / 'bad'
    make_pkg(v1, 'koa2-0.1', 'payload-v1')
    make_pkg(v2, 'koa2-0.2', 'payload-v2')
    make_pkg(bad, 'koa2-0.3', 'payload-v3')
    (bad / 'payload.bin').write_text('corrupted')

    assert run_sh(INSTALL, base, [v1], release='5.10.0') == 'result=refused_unknown_kernel'
    assert run_sh(ROLLBACK, base) == 'result=refused_no_previous_version'
    assert run_sh(INSTALL, base, [v1]) == 'result=install_ok'
    assert (base / 'current/payload.bin').read_text() == 'payload-v1'

    # Corrupted upgrade must be refused and leave v1 untouched.
    assert run_sh(INSTALL, base, [bad]) == 'result=refused_checksum_mismatch'
    assert (base / 'current/payload.bin').read_text() == 'payload-v1'

    # Valid upgrade preserves v1 in prev/.
    assert run_sh(INSTALL, base, [v2]) == 'result=upgrade_ok'
    assert (base / 'current/payload.bin').read_text() == 'payload-v2'
    assert (base / 'prev/payload.bin').read_text() == 'payload-v1'

    # Rollback restores v1 cleanly.
    assert run_sh(ROLLBACK, base) == 'result=rollback_ok'
    assert (base / 'current/payload.bin').read_text() == 'payload-v1'

print('KOA2 installer/rollback: manifest verify, install, upgrade, corrupt refusal, and rollback passed')
