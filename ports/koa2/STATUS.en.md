# Where the port stands

Chinese: [STATUS.md](STATUS.md). The device procedure is
[device-runbook.md](device-runbook.en.md) and the plan is
[ROADMAP.md](ROADMAP.en.md).

## In one sentence

Everything the handoff needs on the host is written and tested (59 host tests
pass, none fail); **nothing has run on the device**, no new kernel has booted,
and no module has been loaded into the stock kernel.

## Where this sits right now

| Question | Answer |
|---|---|
| What is next | The user runs the staging test once, following [device-runbook.md](device-runbook.en.md) |
| The likely result | `insmod_refused`: the module is built against the headless-smoke tree and its symbol CRCs do not match stock |
| What follows that | Check the CRCs against the stock `.ko` files the device copied back, replace the imports that disagree, rebuild, run again |
| What blocks Android | The EPDC source gap ([epdc-source-gap.md](epdc-source-gap.md)): the stock kernel tree cannot be built |
| Anything irreversible done | No. Every device action so far has been a read, or a write to a report directory under `/mnt/us` |

## The handoff piece by piece

| Piece | Code | Test | On the device |
|---|---|---|---|
| The DTB and memory contract | `boot/koa2/contract.c` | `koa2-contract` | The read-only checker has run |
| The layout check | `boot/koa2/plan.c` | `koa2-plan` | Not run |
| The FDT rewrite | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | Not run |
| The copy list and its audit | `boot/koa2/copylist.c` | `koa2-copylist` | Not run |
| The staging module (no jump) | `boot/koa2/stage.c` | `koa2-stage` | Never loaded |
| Stock CRC checking | `ports/koa2/module-crcs.py` | `koa2-module-crcs` | The stock modules are not in hand |
| The jump trampoline | `boot/koa2/jump.S` | `koa2-jump-baremetal` | In no module, never executed |
| The quiesce sequence | `boot/koa2/quiesce.c` | `koa2-quiesce` | Only the order; the hardware bodies are missing |
| Build reproducibility | `ports/koa2/compare-kernel-build.py` | `koa2-compare-build` | Not applicable |

## The current artifacts

| Artifact | Value |
|---|---|
| Kernel release | `4.1.15-lab126-koa2-headless-smoke`, with 97 modules' vermagic matching |
| Image (build-id zeroed) | `e2eba61b8420…`, the same after a rebuild in another output directory |
| zImage | `4921c4b2…`, different on every build (the lzop timestamp); pinned in target-lock.h |
| DTB | `fbbe9517…` |
| What the new kernel writes | Page directory at 0x80004000, decompressed to 0x80008000, _end at 0x80c55a0c |
| The handoff layout | zImage 0x80d00000, FDT 0x81400000, initramfs 0x81500000 |

## The earlier record

The day-by-day log lives in [worklog.md](worklog.md), in Chinese only.
