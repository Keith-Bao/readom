# Where the port stands

Chinese: [STATUS.md](STATUS.md). The device procedure is
[device-runbook.md](device-runbook.en.md) and the plan is
[ROADMAP.md](ROADMAP.en.md).

## In one sentence

The device staging test passed on 2026-11-21 (Asia/Shanghai): sealed_plan_valid.
The module loaded, validated the layout, FDT rewrite and copy list, and unloaded.
No new kernel has booted. On 2026-11-22 the pre-jump marker's write was wired
into the module (`K2S_MARK`); it is not a jump and does not open the way to one.
65 host tests pass and none fail.

## Where this sits right now

| Question | Answer |
|---|---|
| What is next | Two things on the device, neither touching the jump: place a marker, restart, confirm it is still there (`marker-test.sh`, which also tests whether the reset really does preserve DRAM), and the quiesce dry run (`quiesce-dryrun.sh`, which stops the devices and puts them all back) |
| Observed result | `sealed_plan_valid`; insmod_exit=0, client_exit=0, rmmod_exit=0 |
| What follows that | The stock modules have been checked ([stock-module-abi.md](stock-module-abi.en.md)): 12 symbols differ from the build tree, and the module imports none of them |
| What blocks Android | The EPDC source gap ([epdc-source-gap.md](epdc-source-gap.md)): the stock kernel tree cannot be built |
| Anything irreversible done | No. The staging module loaded and unloaded; no jump or partition write was executed |
| Can a failed jump come back | The analysis is [recovery.md](recovery.en.md): the watchdog ends it within 60 seconds and the log survives in the printk buffer; two of its conditions are not met yet |

## The handoff piece by piece

| Piece | Code | Test | On the device |
|---|---|---|---|
| The DTB and memory contract | `boot/koa2/contract.c` | `koa2-contract` | The read-only checker has run |
| The layout check | `boot/koa2/plan.c` | `koa2-plan` | Passed in device staging |
| The FDT rewrite | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | Passed in device staging |
| The copy list and its audit | `boot/koa2/copylist.c` | `koa2-copylist` | Passed in device staging |
| The staging module (no jump) | `boot/koa2/stage.c` | `koa2-stage` | Loaded, sealed and unloaded successfully |
| Stock CRC checking | `ports/koa2/module-crcs.py`, `koa2-stock-abi` | `koa2-module-crcs` | 63 stock modules checked offline: all 31 imports agree |
| The jump trampoline | `boot/koa2/jump.S` | `koa2-jump-baremetal` | In no module, never executed |
| The quiesce sequence | `boot/koa2/quiesce.c`, `quiesce-verify.c` | `koa2-quiesce`, `koa2-quiesce-verify`, `koa2-quiesce-dryrun` | Stopping the devices moved to userspace (section six of [handoff-quiesce.md](handoff-quiesce.md)); the dry run is in the kit and has not been run on the device |
| The pre-jump marker | `boot/koa2/marker.c`, `K2S_MARK` in the module | `koa2-marker`, `koa2-marker-wiring`, `koa2-stage` | All 35 imports match stock CRCs; no marker placed on the device yet |
| Build reproducibility | `ports/koa2/compare-kernel-build.py` | `koa2-compare-build` | Not applicable |

## The module ABI (settled 2026-11-21)

Why `insmod` succeeded is now understood; see
[stock-module-abi.md](stock-module-abi.en.md). modversions skips the release
string, and the 31 symbols the module imports happen to carry the same CRCs as
stock. The build tree and stock disagree on 12 of the 1129 symbols both know:
nine are framebuffer symbols, from the panel being off behind the EPDC gap, and
three come from `CONFIG_FALCON_WRAPPER` making `kernel/sched/core.c` include one
more header. The module is now compiled against a new `out-module-abi` tree,
where those three match stock; the payload is still `out-headless-smoke`.
`cpu_down` is imported by no stock module, so it cannot be checked and only
`insmod` on the device will tell.

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
