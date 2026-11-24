#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Host tests for ports/koa2/stage-test/cpu1-quiesce.sh."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/stage-test/cpu1-quiesce.sh'

HOOK_TEMPLATE = """
uname() { printf '%s\n' '__RELEASE__'; }
dmesg() { printf '[  10.0] CPU1: shutdown\n[  12.0] CPU1: Booted secondary processor\n'; }
sleep() {
    : > "__FAKE__/slept"
    if [ "__MOVE_IRQS__" = "1" ]; then
        printf '           CPU0       CPU1\n 16:       1050        260     GIC  arch_timer\n' > "__FAKE__/proc/interrupts"
    fi
}
cat() {
    if [ "$1" = "__FAKE__/sys/devices/system/cpu/online" ]; then
        v=$(command cat "__FAKE__/sys/devices/system/cpu/cpu1/online" 2>/dev/null)
        if [ "__INIT_ONLINE__" != "0-1" ]; then
            printf '%s\n' "__INIT_ONLINE__"
        elif [ "$v" = "0" ] && [ "__FAIL_OFF__" = "0" ]; then
            printf '0\n'
        elif [ "$v" = "1" ] && { [ "__FAIL_RESTORE__" = "0" ] || [ ! -f "__FAKE__/slept" ]; }; then
            printf '0-1\n'
        else
            printf '0\n'
        fi
        return 0
    fi
    if [ "$1" = "__FAKE__/sys/devices/system/cpu/cpu1/online" ]; then
        v=$(command cat "$1" 2>/dev/null)
        if [ "$v" = "0" ] && [ "__FAIL_OFF__" = "1" ]; then
            printf '1\n'
            return 0
        fi
        if [ "$v" = "1" ] && [ "__FAIL_RESTORE__" = "1" ] && [ -f "__FAKE__/slept" ]; then
            printf '0\n'
            return 0
        fi
    fi
    command cat "$@"
}
"""


def run_cpu1(release='4.1.15-lab126', initial_online='0-1', initial_cpu1='1',
             fail_offline=False, fail_restore=False, move_irqs=False):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / 'mnt/us/documents').mkdir(parents=True)
        cpu_dir = root / 'sys/devices/system/cpu'
        (cpu_dir / 'cpu1').mkdir(parents=True)
        (cpu_dir / 'online').write_text(initial_online + '\n')
        (cpu_dir / 'cpu1/online').write_text(initial_cpu1 + '\n')
        (root / 'proc').mkdir(parents=True)
        (root / 'proc/interrupts').write_text(
            '           CPU0       CPU1\n'
            ' 16:       1000        250     GIC  arch_timer\n'
        )
        fake = str(root)
        text = SCRIPT.read_text(encoding='utf-8')
        for prefix in ('/mnt/us', '/proc/', '/sys/'):
            text = text.replace(prefix, fake + prefix)

        hook = (HOOK_TEMPLATE
                .replace('__RELEASE__', release)
                .replace('__FAKE__', fake)
                .replace('__INIT_ONLINE__', initial_online)
                .replace('__FAIL_OFF__', '1' if fail_offline else '0')
                .replace('__FAIL_RESTORE__', '1' if fail_restore else '0')
                .replace('__MOVE_IRQS__', '1' if move_irqs else '0'))

        script_path = root / 'cpu1.sh'
        lines = text.splitlines(True)
        script_path.write_text(lines[0] + hook + ''.join(lines[1:]), encoding='utf-8')
        env = os.environ.copy()
        env['KOA2_CPU1_WINDOW'] = '0'
        res = subprocess.run([shutil.which('sh'), str(script_path)], env=env,
                             capture_output=True, text=True, check=False)
        assert res.returncode == 0, res.stderr
        run_id = (root / 'mnt/us/koa2-cpu1-quiesce/LAST_RUN.txt').read_text().strip()
        report = (root / 'mnt/us/koa2-cpu1-quiesce' / run_id / 'report.txt').read_text()
        final_cpu1 = (cpu_dir / 'cpu1/online').read_text().strip()
        return report, final_cpu1


class Cpu1QuiesceTests(unittest.TestCase):
    def test_happy_path_restores_cpu1(self):
        report, final = run_cpu1()
        self.assertIn('result=cpu1_quiesce_restored', report)
        self.assertIn('CPU1: shutdown', report)
        self.assertEqual(final, '1')

    def test_refuses_unknown_kernel(self):
        report, final = run_cpu1(release='5.15.0')
        self.assertIn('result=refused_unknown_kernel', report)
        self.assertEqual(final, '1')

    def test_refuses_already_single_core(self):
        report, _ = run_cpu1(initial_online='0', initial_cpu1='0')
        self.assertIn('result=refused_unexpected_cpu_state', report)

    def test_detects_offline_failure_and_restores(self):
        report, final = run_cpu1(fail_offline=True)
        self.assertIn('result=cpu1_did_not_go_offline', report)
        self.assertEqual(final, '1')

    def test_detects_restore_failure(self):
        report, _ = run_cpu1(fail_restore=True)
        self.assertIn('result=cpu1_restore_failed_restart_required', report)

    def test_detects_irqs_while_offline(self):
        report, final = run_cpu1(move_irqs=True)
        self.assertIn('result=cpu1_irqs_moved_while_offline', report)
        self.assertEqual(final, '1')


if __name__ == '__main__':
    res = unittest.main(exit=False, verbosity=0).result
    if not res.wasSuccessful():
        raise SystemExit(1)
    print('KOA2 CPU1-only hotplug: offline/online verification, IRQ silence, failure rollback (6 cases)')
