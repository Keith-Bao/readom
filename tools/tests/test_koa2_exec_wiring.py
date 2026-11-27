#!/usr/bin/env python3
"""The execute module's wiring: it arms the trampoline but cannot jump.

Stage F adds a second module from the same source (boot/koa2/exec.c, pulled
into stage.c under KOA2_EXEC_BUILD) that links the real jump.S and fills its
parameter block, for a dry-run that stops one step short of disabling the
caches. The whole point is that this batch does NOT jump, so the wiring has to
guarantee it: nothing in exec.c branches into the trampoline, it imports no
symbol that stops a core or leaves the kernel, and it reads the core count the
module's own way (cpu_online, whose cpu_online_mask a stock module carries a
CRC for) rather than through num_online_cpus, which would pull in an unchecked
__sw_hweight. The build gate's Gate 2 is checked here on the source side; the
on-device CRC gate is build-exec-module.sh's Gate 3.
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
EXEC = (ROOT / 'boot/koa2/exec.c').read_text(encoding='utf-8')
EXEC_CODE = re.sub(r'/\*.*?\*/', ' ', EXEC, flags=re.S)
STAGE = (ROOT / 'boot/koa2/stage.c').read_text(encoding='utf-8')
KBUILD = (ROOT / 'boot/koa2/Kbuild').read_text(encoding='utf-8')
BUILD = (ROOT / 'ports/koa2/build-exec-module.sh').read_text(encoding='utf-8')
ABI = (ROOT / 'boot/koa2/stage-abi.h').read_text(encoding='utf-8')
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


# 1. exec.c never calls the trampoline. It may name koa2_jump_start/_end (to
#    take the blob's size and copy it) but must never branch to it: no
#    "koa2_jump_start(" or a function-pointer call through it.
check('koa2_jump_start(' not in EXEC_CODE, 'exec.c calls koa2_jump_start()')
check('koa2_jump_params(' not in EXEC_CODE, 'exec.c calls koa2_jump_params()')
# The control page is filled and freed; there is no enter()/bx into it.
check(re.search(r'\benter\s*\(', EXEC_CODE) is None, 'exec.c has an enter() call')

# 2. The core count comes from cpu_online, not num_online_cpus (which imports
#    an __sw_hweight no stock module carries a CRC for).
check('cpu_online(' in EXEC_CODE, 'exec.c does not read cpu_online()')
check('num_online_cpus' not in EXEC_CODE, 'exec.c uses num_online_cpus (unchecked import)')

# 3. No symbol that stops a core, restarts, or leaves the kernel.
for name in ('cpu_down', 'machine_restart', 'kernel_restart', 'soft_restart',
             'kallsyms_lookup_name', '__dump_lastk_to_mmc', 'disable_nonboot_cpus'):
    check(name not in EXEC_CODE, f'exec.c references {name}')

# 4. execute_available is written 0 and never anything else, in both files.
for blob, name in ((EXEC_CODE, 'exec.c'), (STAGE, 'stage.c')):
    for m in re.finditer(r'execute_available\s*=\s*([^;]+);', blob):
        check(m.group(1).strip() in ('0', '0u'), f'{name} sets execute_available to {m.group(1).strip()!r}')

# 5. The dry-run masks IRQ and FIQ around its read and releases both.
check('local_irq_save' in EXEC_CODE and 'local_irq_restore' in EXEC_CODE, 'exec.c does not save/restore IRQ')
check('local_fiq_disable' in EXEC_CODE and 'local_fiq_enable' in EXEC_CODE, 'exec.c does not disable/enable FIQ')
# It stops before the caches: no SCTLR write (mcr to c1,c0,0) and no cache
# disable. The only CP15 touches are mrc reads.
check(re.search(r'mcr\b', EXEC_CODE) is None, 'exec.c writes a coprocessor register')
check('flush_cache_all' not in EXEC_CODE and 'cpu_proc_fin' not in EXEC_CODE, 'exec.c disables caches')

# 6. Kbuild builds the exec module only under KOA2_EXEC, with jump.S and the
#    two defines, and the staging module without them.
check('ifdef KOA2_EXEC' in KBUILD, 'Kbuild has no KOA2_EXEC branch')
check('koa2_handoff_exec' in KBUILD and 'jump.o' in KBUILD, 'Kbuild exec target missing jump.o')
check('-DKOA2_EXEC_BUILD' in KBUILD and '-DKOA2_JUMP_BUILD' in KBUILD, 'Kbuild exec defines missing')
check('jump.o' not in KBUILD.split('else', 1)[1], 'staging module links jump.o')

# 7. The build gate is mandatory on the CRC check and keeps CP15 writes inside
#    the trampoline.
check('KOA2_STOCK_MODULES is required' in BUILD, 'build-exec-module.sh does not require stock modules')
check('outside the trampoline' in BUILD, 'build-exec-module.sh does not bound CP15 writes')
check('branch at' in BUILD and 'into the trampoline' in BUILD, 'build-exec-module.sh does not forbid branches into the trampoline')

# 8. The ioctl is defined only for the exec build, and the report carries the
#    stopped-before-caches and execute_available fields.
check('K2S_EXEC_DRYRUN' in ABI and 'stopped_before_caches' in ABI, 'stage-abi.h missing exec dry-run fields')
check('#ifdef KOA2_EXEC_BUILD' in STAGE and 'case K2S_EXEC_DRYRUN' in STAGE, 'stage.c does not gate the exec ioctl')

if failures:
    for f in failures:
        print('FAIL', f)
    raise SystemExit(1)
print('KOA2 exec wiring: no jump, cpu_online not num_online_cpus, execute_available stays 0, '
      'IRQ/FIQ masked and released, no CP15 write, gates mandatory and trampoline-bounded')
