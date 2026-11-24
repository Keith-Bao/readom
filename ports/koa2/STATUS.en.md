# Where the port stands

Chinese: [STATUS.md](STATUS.md). The device procedure is
[device-runbook.md](device-runbook.en.md) and the plan is
[ROADMAP.md](ROADMAP.en.md).

## In one sentence

The device staging test passed on 2026-11-21 (Asia/Shanghai): sealed_plan_valid.
The module loaded, validated the layout, FDT rewrite and copy list, and unloaded.
No new kernel has booted. On 2026-11-22 the pre-jump marker's write was wired
into the module (`K2S_MARK`); it is not a jump and does not open the way to one.
26 KOA2 host tests pass and none fail.
On 2026-11-23 the script that reads the marker back moved to `tr` and now
requires the read to prove itself: it used `strings`, which this device is not
known to have, and a missing applet read as `marker_absent`.
The original kit and its operating sheet are archived in
[the materials folder](materials/2026-11-23/README.en.md).

On 2026-11-23 the marker survived a normal restart; on 2026-11-24 stock suspend passed (`stock_sleep_touch_observed`) and isolated USB reload recovered mass storage. Host work on 2026-11-24 also completed the recovery/early-logging audit, Lab126 printk D-cache write-back, combined QEMU SELinux `policy30` enforcing + multi-context Binder smoke, unified Android 4.1 kernel build, clean-room v1 EPDC helper ARM compile (`epdc-v1`), and `device/amazon/koa2/` hardware contracts. The 2026-11-24 device kit now covers 5 tests (12 SHA256 entries); see [the 2026-11-24 sheet](materials/2026-11-24/README.en.md).

## Where this sits right now

| Question | Answer |
|---|---|
| What is next | Run the [2026-11-24 5-item kit](materials/2026-11-24/README.en.md) on the Kindle (read-only EPDC/Wi-Fi/DTB, CPU1-only, touch-only), then design EPDC stop and rerun reversible quiesce |
| Observed result | Staging `sealed_plan_valid`, restart marker `marker_survived`, USB-only reload passed, stock sleep `stock_sleep_touch_observed`, original dry run `incomplete_see_above` |
| What follows that | The stock modules have been checked ([stock-module-abi.md](stock-module-abi.en.md)): 12 symbols differ from the build tree, and the module imports none of them |
| What blocks Android | The EPDC source gap ([epdc-source-gap.md](epdc-source-gap.md)): stock tree still lacks the file; isolated v1 (`epdc-v1`, night mode rejected) compiles `mxc_epdc_v2_fb.o` but is untested on hardware |
| Anything irreversible done | No. The staging module loaded and unloaded; no jump or partition write was executed |
| Can a failed jump come back | See [recovery.md](recovery.en.md): printk D-cache write-back is now in the experimental kernel; real `WDOG_B` survival awaits reversible quiesce and the jump stage |

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
| The quiesce sequence | `boot/koa2/quiesce.c`, `quiesce-verify.c` | `koa2-quiesce`, `koa2-quiesce-verify`, `koa2-quiesce-dryrun` | The original dry run failed. The device entry now runs a read-only preflight. |
| The pre-jump marker | `boot/koa2/marker.c`, `K2S_MARK` in the module | `koa2-marker`, `koa2-marker-wiring`, `koa2-stage` | Placed and recovered across a normal restart on the device |
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
| zImage | `d2a93246…` (2026-11-24 rebuild with printk write-back; changes per build, pinned in target-lock.h) |
| DTB | `fbbe9517…` |
| What the new kernel writes | Page directory at 0x80004000, decompressed to 0x80008000, _end at 0x80c55a0c |
| The handoff layout | zImage 0x80d00000, FDT 0x81400000, initramfs 0x81500000 |

## The earlier record

The day-by-day log lives in [worklog.md](worklog.md), in Chinese only.

[Device report and continuation notes (Chinese)](reports/2026-11-21-stage-test/README.md)。

[Continue on another computer](CONTINUE.en.md): no device is needed for the next host work.
