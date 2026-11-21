# The stock kernel's module ABI (established on the device, 2026-11-21)

Chinese: [stock-module-abi.md](stock-module-abi.md).

In the staging test of 2026-11-21 `insmod` succeeded (the report is
[reports/2026-11-21-stage-test/report.txt](reports/2026-11-21-stage-test/report.txt)),
so the CRCs the module carries for its 31 imports and for `module_layout` are
the ones the stock kernel exports. The 63 stock modules the device copied back
are in
[materials/2026-11-21/device-evidence/stock-modules](materials/2026-11-21/device-evidence/stock-modules),
which makes that a fact a test can hold rather than a result to remember. The
test is `koa2-stock-abi`.

## The earlier expectation was wrong

What was written down was "insmod_refused is the likely one", because the
module is built against the headless-smoke tree and its CRCs would not match.
It loaded. Two things make that so:

- with CRCs present, `same_magic` skips the release string, so
  `4.1.15-lab126-koa2-headless-smoke` against `4.1.15-lab126` is not by itself
  a refusal;
- the 31 symbols this module imports happen to carry the same CRCs as stock.

"Happen to" is the honest phrase: the build tree and stock disagree on 12 of
the 1129 symbols both know, and the module imports none of them.

## The 12 that differ, and why each does

| Symbols | Why |
|---|---|
| `cfb_copyarea`, `cfb_fillrect`, `cfb_imageblit`, `fb_set_var`, `framebuffer_alloc`, `framebuffer_release`, `lock_fb_info`, `register_framebuffer`, `unregister_framebuffer` | The build tree has the EINK panel off (the source gap is [epdc-source-gap.md](epdc-source-gap.md)). The module imports none of them |
| `set_cpus_allowed_ptr`, `sched_setscheduler`, `wake_up_process` | `CONFIG_FALCON_WRAPPER`. Under that option `kernel/sched/core.c` does `#include <linux/falcon_storage.h>`, and genksyms folds it into the CRC of every symbol that file exports |

The last three were settled by experiment: turning `FALCON_WRAPPER` back on in
the build tree moves all three to the stock values (`0xe632f7cc`,
`0xd09a0a5e`, `0xd7e0ebd8`), and leaves the nine framebuffer symbols where they
were.

## So there are two trees

| Tree | Configuration | What it is for |
|---|---|---|
| `out-module-abi` | headless-smoke plus `FALCON_WRAPPER=y`, panel still off | **The tree the module is compiled against**. Its zImage is not a kernel to boot |
| `out-headless-smoke` | The panel off and Falcon off | **The new kernel that would boot** (the payload), which keeps the property of not writing raw eMMC sectors |

`build-stock-kernel.sh module-abi` builds the first, and builds only vmlinux
and modules, because `Module.symvers` is all that is wanted from it.
`build-stage-test-kit.sh` already follows the split: the module is compiled
against `out-module-abi` and the payload is `out-headless-smoke`'s zImage.

This is not fixing a present problem, since the 31 imports already match. It is
for the next stage: the quiesce sequence needs `cpu_down`, which lives in
`kernel/cpu.c` and is untouched by either option, but the moment anything
exported from `kernel/sched/core.c` is used, compiling against this tree is
already right.

## cpu_down cannot be checked

No stock module imports `cpu_down`, so its CRC cannot be read off the device
and **cannot be checked before loading**. Two very differently configured trees
both give `0x7cb1ae69`, which makes it look stable, but that is not evidence.
The only test is `insmod` on the device: if it disagrees, the kernel refuses
cleanly rather than crashing. The test `koa2-stock-abi` keeps that written down
as a case, so the next person does not assume it was checked.
