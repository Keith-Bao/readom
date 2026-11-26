# KOA2 next device visit (2026-11-26 kit)

This file is the operating sheet. Run the steps in order on the Kindle; no WSL or rebuild is needed. Chinese: [README.md](README.md).

What the 2026-11-25 device visit established, and what this kit changes:
- **EPDC stop/reload works.** `stop x` released all 3 `/dev/fb0` references (`epdc_users_after_stop=0`), the module unloaded (0.07 s) and reloaded with its exact waveform (0.21 s). Only the restore hung: `initctl start x` never returned. This kit's `epdc-quiesce.sh` is **v3**, which starts x with `--no-wait` (the stock `/etc/upstart/display` path), so the call returns at once and the 30 s fb0-reopen poll decides success.
- **The combined-freeze cause is `rmmod g_mass_storage`.** The USB+Touch pair froze exactly at `stage=usb_unload`, before touch. Touch+CPU1 passed cleanly. This kit's `full-quiesce-dryrun.sh` is **v3**, which no longer unloads the gadget module: it writes `disconnect` to the UDC `soft_connect` node and `connect` to restore, so the controller stops and restarts synchronously with no `rmmod` teardown wait.

| # | KUAL entry | Question it answers | What it changes |
|---|---|---|---|
| 1 | **EPDC only: stop X, unload and reload (reversible)** | Does `start x --no-wait` bring the display back? (the one step that hung on 2026-11-25) | X/framework down ~15 s, restored by script |
| 2 | Combined pair: USB + Touch only | Does the `soft_connect` USB disconnect (no `rmmod`) avoid the freeze that `rmmod g_mass_storage` caused? | USB and touch ~2 s, restored by script |
| 3 | Combined dry-run: USB + Touch + CPU1 | Do all three chained together come back in full with the new USB path? | All three ~3 s, restored in reverse by script |

Entry 1 reloads the EPDC module but never jumps, writes partitions, or loads the handoff module. Entries 2-3 do not touch EPDC. Every step writes and syncs a `stage=` line before acting, so after a freeze the report's last `stage=` line names the step that did not return. Writes go only to the `koa2-epdc-quiesce/` and `koa2-full-quiesce-dryrun/` reports.

## 0. Check first

Oasis 2, firmware 5.16.2.1.1, jailbroken with KUAL, battery above 50%. Stop if not; the scripts also refuse other kernels.

## 1. Copy the kit to the Kindle

Fetch only this folder:

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-26
git checkout
```

Remove any older `extensions/readom-koa2-stage-test` first. Copy `ports/koa2/materials/2026-11-26/readom-koa2-stage-test` into the Kindle's `extensions/` directory, then verify on the Kindle drive (adjust the drive letter):

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

All 14 lines must be `OK`, and the second command must print `{"items"`. Seeing `%TSD-Head` means corporate DLP encrypted the files; copy from a personal computer instead. Eject USB safely.

## 2. Entry 1 - EPDC stop and reload (the v3 restore fix)

**Unplug the USB cable** and stay in the stock UI with the screen lit. KUAL -> **Readom KOA2 Stage Test** -> **EPDC only: stop X, unload and reload (reversible)**.
The screen goes blank for up to ~15 s while X stops, the module reloads, and X restarts. The report is in `koa2-epdc-quiesce/<run>/report.txt`.

| result | Meaning | Next step |
|---|---|---|
| `epdc_quiesce_restored` | **Desired result**: module reloaded and X came back; `x_started=1`, `fb_restored=1`, and `x_status_after=x start/running` | Tap the screen to confirm touch, go to step 3 |
| `epdc_gui_restore_timeout` | Module reloaded but Xorg did not reopen fb0 in 30 s; `x_status_after`/`lab126_gui_status_after` show where it stalled | Bring back the report; normal restart restores the desktop |
| `epdc_x_start_failed_restart_required` | `start x --no-wait` itself failed | Bring back the report, restart |
| `epdc_reload_failed_restart_required` | Module did not reload | Bring back the report, restart |
| `refused_*` | A precondition failed; nothing changed | Fix the cause, retry |

If the display does not return on its own, a normal restart always brings the stock desktop back (the module and waveform are stock).

## 3. Combined dry-runs with the new USB path

Still unplugged and in the stock UI, **do not press power or lock the screen**. Run entry **USB + Touch only**, then **USB + Touch + CPU1**, reading the last line of each report first:

| result | Meaning | Next step |
|---|---|---|
| `full_quiesce_dryrun_restored` | **Desired result**: every step stopped and came back; USB shows `step_usb=disconnected` then `restore_usb=ok` | Tap to confirm touch, go to the next entry |
| `step_usb_not_stopped` | The UDC did not leave the configured state | Bring back the report, skip the rest |
| `dryrun_restore_failed_restart_required` | A step did not come back; `restore_*=failed` names it | Normal restart, bring back the report, skip the rest |
| `step_*` | A step did not stop; earlier ones were restored | Bring back the report, skip the rest |
| `refused_usb_host_connected` | USB is still plugged into a host | Unplug, retry |
| `refused_*` | Another precondition failed; nothing changed | Fix the cause, retry |

**If it freezes** (screen stuck, touch and buttons dead): wait up to about a minute for a self-restart; if none, hold power ~40 s.
Back in the stock UI, **first** run KUAL -> **Read previous boot recovery log**, then stop testing and bring back the reports. Do not retry the same entry.

After the USB+Touch+CPU1 entry passes, plug USB back in and check that Windows opens the Kindle drive normally.

## 4. Bring back reports and clean up

```bash
cp -r /d/koa2-epdc-quiesce /d/koa2-full-quiesce-dryrun ~/koa2-evidence/
```

After checking the copies on the PC, remove `extensions/readom-koa2-stage-test` and both report directories from the Kindle. Keep raw reports local only; never commit them to Git.

## 5. Next hardware gates

1. With EPDC reversible stop/reload and the `soft_connect` USB path both confirmed, every peripheral has a proven reversible quiesce.
2. Only then do the first watchdog-guarded `headless-smoke` jump (see [recovery.md](../../recovery.en.md)).

## 6. Provenance of this kit

The binaries (`zImage`, `koa2_handoff_stage.ko`, `initramfs.cpio.gz`, `koa2-stage-client`, `touch-events`) are byte-for-byte the device-validated 2026-11-24/25 build: 35 stock-CRC imports match, see [crcs.json](crcs.json). Only `epdc-quiesce.sh` (v3) and `full-quiesce-dryrun.sh` (v3) changed from the 2026-11-25 kit.
Checksums for the 14 files are in `readom-koa2-stage-test/SHA256SUMS`, and for the whole folder in [SHA256SUMS](SHA256SUMS). The host tests `koa2-epdc-quiesce` and `koa2-full-quiesce-dryrun` cover every failing step, every failing restore and every refusal.
See the [quiesce audit](../../handoff-quiesce.md) and [device runbook](../../device-runbook.en.md).
