# Five tests to run at home (2026-11-24 kit)

This file is the operating sheet. Run the steps in order on the Kindle; no WSL or rebuild is needed. Chinese: [README.md](README.md).

| # | Test | Question it answers | What it changes |
|---|---|---|---|
| 1 | Inspect EPDC framebuffer users | What holds the 3 EPDC module references | Read-only; stops nothing |
| 2 | Inspect Wi-Fi (bcmdhd) users & params | `bcmdhd` module refs, safe params and userspace daemons | Read-only; stops nothing |
| 3 | Backup stock runtime DTB (read-only) | Captures the missing local backup of `/sys/firmware/fdt` | Read-only; copies into report dir |
| 4 | CPU1 only: offline and online | Whether stock GPCv2 secondary-core offline/online works cleanly | CPU1 offline ~2 s, restored by script |
| 5 | Touch only: sleep and wake controller | Whether the I²C bus goes quiet and touch recovers after wake | Touch off ~12 s, woken by script |

None of the five loads the handoff module, jumps, writes partitions, or unloads drivers. Writes go only to `koa2-quiesce-dryrun/`, `koa2-cpu1-quiesce/`, and `koa2-touch-quiesce/` on the Kindle.

## 0. Check first

Oasis 2, firmware 5.16.2.1.1, jailbroken with KUAL, battery above 50%. Stop if not; the scripts also refuse other kernels.

## 1. Copy the kit to the Kindle

Fetch only this folder:

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-24
git checkout
```

Remove any older `extensions/readom-koa2-stage-test` first. Copy `ports/koa2/materials/2026-11-24/readom-koa2-stage-test` into the Kindle's `extensions/` directory, then verify on the Kindle drive (adjust the drive letter):

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

All 12 lines must be `OK`, and the second command must print `{"items"`. Seeing `%TSD-Head` means corporate DLP encrypted the files; copy from a personal computer instead. Eject USB safely.

## 2. Three read-only inspections (USB may stay connected)

In KUAL -> **Readom KOA2 Stage Test**, run these three in order (each takes a few seconds, screen unchanged; reports land in `koa2-quiesce-dryrun/<run>/report.txt`, ending with `result=quiesce_blocked_pending_restore`):

1. **Inspect EPDC framebuffer users**: check `[epdc_users]` (`epdc_reference_accounting` and process list).
2. **Inspect Wi-Fi (bcmdhd) users & params**: check `[wifi_users]` (module refs, interface state, SDIO binding and daemon comms; no MAC/SSID/credentials).
3. **Backup stock runtime DTB (read-only)**: check `[runtime_fdt]` for `fdt_backup=ok`, `fdt_magic=d00dfeed` and SHA256; `stock-runtime.dtb` is saved beside the report (keep local only, **never commit to Git**).

## 3. CPU1-only offline and online

KUAL -> **CPU1 only: offline and online**. Completes in a few seconds; report in `koa2-cpu1-quiesce/<run>/report.txt`:

| result | Meaning | Next step |
|---|---|---|
| `cpu1_quiesce_restored` | **Desired result**: CPU1 went offline with no IRQs and came back online | Continue to next step |
| `cpu1_restore_failed_restart_required` | CPU1 did not come back online | Do a normal restart and bring back the report; do not retry |
| Other `cpu1_*` / `refused_*` | Report explains why | Bring back the report; do not retry |

## 4. Touch-only sleep and wake

Unplug USB, stay in the stock UI with the screen lit, and **do not press the power button or lock the screen during the test**.

KUAL -> **Touch only: sleep and wake controller (tap after)**. Touch is expected not to respond for the first ~12 seconds; tap the screen once within ten seconds after that.

Report in `koa2-touch-quiesce/<run>/report.txt`:

| result | Meaning | Next step |
|---|---|---|
| `touch_quiesce_restored_touch_observed` | **Desired result**: zero bus/touch IRQs while asleep, power off, touch seen after wake | Bring back report |
| `touch_quiesce_restored_touch_unconfirmed` | Sleep verified, no tap seen in 10 s after wake | Tap stock UI to confirm touch works, bring back report |
| `touch_wake_failed_restart_required` | Wake did not succeed | Normal restart restores touch; do not retry |
| Other `touch_*` | Report explains what failed; wake was attempted | Confirm touch works, bring back report; do not retry |
| `refused_*` | Precondition not met; nothing written | Bring back report |

If touch still does not respond afterwards: normal restart. If completely unresponsive, hold power for about 40 seconds.

## 5. Bring back reports and clean up

```bash
cp -r /d/koa2-quiesce-dryrun /d/koa2-cpu1-quiesce /d/koa2-touch-quiesce ~/koa2-evidence/
```

After verifying the copies and `stock-runtime.dtb` on the PC, remove `extensions/readom-koa2-stage-test` and those three report directories from the Kindle. Keep raw reports and `stock-runtime.dtb` local only; never commit them to Git.

## 6. Provenance of this kit

Built in WSL via `ports/koa2/build-stage-test-kit.sh` (including the rebuilt `zImage` with printk cache write-back and matching staging module). Checksums for all 12 files are in `readom-koa2-stage-test/SHA256SUMS`. None of the five tests loads the handoff module. See the [quiesce audit](../../handoff-quiesce.md) and [device runbook](../../device-runbook.en.md).
