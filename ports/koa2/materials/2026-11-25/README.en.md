# KOA2 next device visit (2026-11-25 kit)

This file is the operating sheet. Run the steps in order on the Kindle; no WSL or rebuild is needed. Chinese: [README.md](README.md).

The last visit (2026-11-25) ran combined dry-run v1, which froze the Kindle. The watchdog restarted it into stock, the report never reached the disk, and nobody knows which step stopped.
The v2 in this kit writes and syncs a `stage=` line **before** each action, so after a freeze the last `stage=` line in the report names the step that did not return.
It also adds two entries that run only two of the three steps, to narrow down which pair causes the trouble.

| # | KUAL entry | Question it answers | What it changes |
|---|---|---|---|
| 1 | Inspect EPDC framebuffer users | The `start on`/`stop on` dependencies of the `x` job, which decide how EPDC can be stopped later | Read-only |
| 2 | **EPDC only: stop X, unload and reload (reversible)** | Stops the X process tree (releases the 3 `/dev/fb0` references), unloads and reloads the EPDC module, restarts X and waits for Xorg to reopen fb0 | X/framework down ~15 s, restored by script |
| 3 | Combined pair: Touch + CPU1 only | Whether taking CPU1 offline while touch sleeps freezes the device (the prime suspect for v1) | Touch ~2 s, CPU1 ~1 s, restored by script |
| 4 | Combined pair: USB + Touch only | Whether sleeping touch right after the USB storage unload freezes the device | USB and touch ~2 s, restored by script |
| 5 | Combined dry-run: USB + Touch + CPU1 | Whether all three chained together come back in full | All three ~3 s, restored in reverse by script |

None of the five loads the handoff module, jumps, writes partitions, or touches EPDC. Writes go only to the `koa2-quiesce-dryrun/` and `koa2-full-quiesce-dryrun/` reports.

## 0. Check first

Oasis 2, firmware 5.16.2.1.1, jailbroken with KUAL, battery above 50%. Stop if not; the scripts also refuse other kernels.

## 1. Copy the kit to the Kindle

Fetch only this folder:

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-25
git checkout
```

Remove any older `extensions/readom-koa2-stage-test` first. Copy `ports/koa2/materials/2026-11-25/readom-koa2-stage-test` into the Kindle's `extensions/` directory, then verify on the Kindle drive (adjust the drive letter):

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

All 14 lines must be `OK`, and the second command must print `{"items"`. Seeing `%TSD-Head` means corporate DLP encrypted the files; copy from a personal computer instead. Eject USB safely.

## 2. Read-only inspection (USB may stay connected)

KUAL -> **Readom KOA2 Stage Test** -> **Inspect EPDC framebuffer users**. Takes a few seconds; the screen does not change.
The report is in `koa2-quiesce-dryrun/<run>/report.txt` and ends with `result=quiesce_blocked_pending_restore`.
Check `epdc_reference_accounting=matched` under `[epdc_users]`, and the lines starting with `epdc_upstart_`.

## 3. Combined dry-runs, one at a time

**Unplug the USB cable**, stay in the stock UI with the screen lit, and **do not press the power button or lock the screen during the tests**. Run 2, then 3, then 4, then 5, reading the last line of each report before going on:

| result | Meaning | Next step |
|---|---|---|
| `full_quiesce_dryrun_restored` | **Desired result**: every step stopped and came back | Tap the screen to confirm touch works, go to the next entry |
| `dryrun_restore_failed_restart_required` | One step did not come back; `restore_*=failed` in the report names it | Normal restart, bring back the report, **skip the remaining entries** |
| `step_*` | One step did not stop; the earlier ones were restored | Bring back the report, skip the remaining entries |
| `refused_*` | A precondition was not met and nothing changed (`refused_lun_not_proven_idle` means USB is still plugged in) | Fix the cause, then the entry may be run again |

**If it freezes** (screen stuck, touch and buttons dead): wait up to about a minute for it to restart by itself; if it does not, hold the power button for about 40 seconds.
Back in the stock UI, **first** run KUAL -> **Read previous boot recovery log**, then stop testing for this visit and bring back the reports. Do not retry the same entry.

After entry 5 passes, plug USB back in and check that Windows opens the Kindle drive normally.

## 4. Bring back reports and clean up

```bash
cp -r /d/koa2-quiesce-dryrun /d/koa2-full-quiesce-dryrun ~/koa2-evidence/
```

After checking the copies on the PC, remove `extensions/readom-koa2-stage-test` and both report directories from the Kindle. Keep raw reports local only; never commit them to Git.

## 5. Next hardware gates

1. Use the `x` job dependencies from step 2 to design and verify, on its own, the `epdc-quiesce.sh` stop and reload.
2. Only once every peripheral passes reversible quiesce, do the first watchdog-guarded `headless-smoke` jump (see [recovery.md](../../recovery.en.md)).

## 6. Provenance of this kit

Built in WSL via `ports/koa2/build-stage-test-kit.sh` with `KOA2_STOCK_MODULES` set, so the build first passed the stock CRC check: all 35 imports match, see [crcs.json](crcs.json).
Checksums for the 14 files are in `readom-koa2-stage-test/SHA256SUMS`, and for the whole folder in [SHA256SUMS](SHA256SUMS).
The `zImage` is the same as in the 2026-11-24 kit (`d2a93246…`, with printk cache write-back). The host test `koa2-full-quiesce-dryrun` covers every failing step, every failing restore and every refusal.
See the [quiesce audit](../../handoff-quiesce.md) and [device runbook](../../device-runbook.en.md).
