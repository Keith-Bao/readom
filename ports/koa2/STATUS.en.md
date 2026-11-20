# Where the port stands

Chinese: [STATUS.md](STATUS.md). The device procedure is
[device-runbook.md](device-runbook.en.md) and the plan is
[ROADMAP.md](ROADMAP.en.md).

## In one sentence

The device staging test passed on 2026-11-21 (Asia/Shanghai): sealed_plan_valid.
The module loaded, validated the layout, FDT rewrite and copy list, and unloaded.
No new kernel has booted. The previous 59 host tests were not rerun this session.

## Where this sits right now

| Question | Answer |
|---|---|
| What is next | Stop device testing; implement hardware quiesce run/undo and capture the live EPDC framebuffer exclusion |
| Observed result | `sealed_plan_valid`; insmod_exit=0, client_exit=0, rmmod_exit=0 |
| What follows that | 63 stock modules recovered; loading succeeded, so no retry for the predicted CRC mismatch |
| What blocks Android | The EPDC source gap ([epdc-source-gap.md](epdc-source-gap.md)): the stock kernel tree cannot be built |
| Anything irreversible done | No. The staging module loaded and unloaded; no jump or partition write was executed |

## The handoff piece by piece

| Piece | Code | Test | On the device |
|---|---|---|---|
| The DTB and memory contract | `boot/koa2/contract.c` | `koa2-contract` | The read-only checker has run |
| The layout check | `boot/koa2/plan.c` | `koa2-plan` | Passed in device staging |
| The FDT rewrite | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | Passed in device staging |
| The copy list and its audit | `boot/koa2/copylist.c` | `koa2-copylist` | Passed in device staging |
| The staging module (no jump) | `boot/koa2/stage.c` | `koa2-stage` | Loaded, sealed and unloaded successfully |
| Stock CRC checking | `ports/koa2/module-crcs.py` | `koa2-module-crcs` | 63 stock modules recovered; offline checking not yet run |
| The jump trampoline | `boot/koa2/jump.S` | `koa2-jump-baremetal` | In no module, never executed |
| The quiesce sequence | `boot/koa2/quiesce.c` | `koa2-quiesce` | Only the order; the hardware bodies are missing |
| Build reproducibility | `ports/koa2/compare-kernel-build.py` | `koa2-compare-build` | Not applicable |

## The current artifacts

| Artifact | Value |
|---|---|
| Kernel release | `4.1.15-lab126-koa2-headless-smoke`, with 97 modules' vermagic matching |
| Image (build-id zeroed) | `e2eba61b8420…`, the same after a rebuild in another output directory |
| zImage | `4c69f47e…`, different on every build (the lzop timestamp); pinned in target-lock.h |
| DTB | `fbbe9517…` |
| What the new kernel writes | Page directory at 0x80004000, decompressed to 0x80008000, _end at 0x80c55a0c |
| The handoff layout | zImage 0x80d00000, FDT 0x81400000, initramfs 0x81500000 |

## The earlier record

The day-by-day log lives in [worklog.md](worklog.md), in Chinese only.

[Device report and continuation notes (Chinese)](reports/2026-11-21-stage-test/README.md)。

[Continue on another computer](CONTINUE.en.md): no device is needed for the next host work.
