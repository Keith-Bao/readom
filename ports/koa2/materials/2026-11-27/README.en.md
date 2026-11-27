# KOA2 next device session (2026-11-27 kit)

This is the operating sheet: do it on the Kindle in order, with no WSL and no rebuild. 中文: [README.md](README.md).

There is one new test this time: **the execute-module dry run**. It takes the jump trampoline `jump.S` onto the device for the first time, without jumping:
it stops X and unloads the EPDC, checks read-only that USB is idle, puts touch to sleep and takes CPU1 offline, then loads `koa2_handoff_exec.ko`,
which re-checks the quiesce state and the copy list, arms the trampoline in a page it owns and fills in its parameters, masks IRQ/FIQ to read the CPU registers once,
and stops before the caches are disabled, putting everything back in reverse. It writes no partition, does not touch the watchdog, and `execute_available` stays 0.

| # | KUAL menu item | What it answers | What it changes |
|---|---|---|---|
| 1 | **Exec dry-run: quiesce, arm trampoline, stop before caches (no jump)** | Does the execute module load into the stock kernel, arm the trampoline with every peripheral quiesced, and restore fully? | Display, touch and CPU1 out for about 20 s; the script restores them in reverse |

Before every action a `stage=` line is written and synced, so after a freeze the last `stage=` line in the report names the step that did not return. Reports go only to `koa2-exec-dryrun/`.

## 0. Confirm first

Oasis 2, firmware 5.16.2.1.1, jailbroken with KUAL, battery above 50%. Otherwise stop; the script also refuses any other kernel.

## 1. Copy the kit to the Kindle

Fetch only this folder:

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-27
git checkout
```

Delete the old `extensions/readom-koa2-stage-test` from the Kindle first. Copy all of `ports/koa2/materials/2026-11-27/readom-koa2-stage-test` into the Kindle's `extensions/`, then check it on the Kindle drive (adjust the drive letter):

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

All 16 lines must say `OK`, and the second command must print `{"items"`. If you see `%TSD-Head`, the company DLP encrypted the files; copy from a personal computer. Then safely eject USB.

## 2. The execute-module dry run

**Unplug the USB cable**, turn Wi-Fi off, stay on the stock home screen with the screen on, and **do not press the power button or lock the screen**. KUAL → **Readom KOA2 Stage Test** → **Exec dry-run: quiesce, arm trampoline, stop before caches (no jump)**.
The screen goes blank for ten seconds or so and the desktop comes back. The report is in `koa2-exec-dryrun/<run>/report.txt`.

| result | Meaning | Next |
|---|---|---|
| `exec_dryrun_restored` | **The result we want**: trampoline armed and given back, quiesce verdict passed; the report shows `exec_stopped_before_caches=1`, `exec_execute_available=0`, `exec_copylist=0`, `exec_params=0`, `exec_quiesce=0` | Tap to confirm touch, bring the report back |
| `exec_dryrun_restored_not_quiesced` | Trampoline armed and given back, but the module's quiesce verdict refused, most likely because `/` is not read-only (`rootfs_readonly=0`) | Not a failure; bring the report back |
| `step_exec_insmod_refused` | The stock kernel refused the execute module; the report carries the version lines from dmesg | Bring the report back; the other peripherals were restored |
| `step_exec_not_armed` | The module loaded, but the copy-list re-audit or the parameter fill failed | Bring the report back |
| `exec_restore_failed_restart_required` | A step did not come back; `restore_*=failed` or `fb_restored=0` says which | Restart normally, bring the report back |
| `step_*` | A step did not complete; the earlier ones were restored | Bring the report back |
| `refused_*` | A precondition did not hold and nothing changed (USB plugged in, Wi-Fi on, CPU1 not online, and so on) | Fix the cause and retry |

**If it freezes** (screen still, no response to touch or buttons): wait for it to restart itself, about a minute at most; if it does not, hold the power button for about 40 s.
Back on the stock home screen, run KUAL → **Read previous boot recovery log** **first**, then bring the report back. Do not retry.

After a pass, plug USB back in and confirm Windows opens the Kindle drive.

## 3. Bring the report back and clean up

```bash
cp -r /d/koa2-exec-dryrun ~/koa2-evidence/
```

Once the copied report is checked on the computer, delete `extensions/readom-koa2-stage-test` and this report folder from the Kindle. Raw reports stay local and are not committed.

## 4. The device gates after this

Only after this passes does the real jump get written: disable the caches, enter the trampoline, boot the `headless-smoke` kernel, and let the watchdog bring the device back to stock. Two things must be settled first: how to feed the watchdog right before the jump, and the exact r1 value at the new kernel's entry. The real jump gets its own kit; see [recovery.en.md](../../recovery.en.md).

## 5. Where this kit comes from

`zImage` and `initramfs.cpio.gz` are byte-identical to the 2026-11-26 kit. New: `koa2_handoff_exec.ko` and `exec-dryrun.sh`, one more menu item, and `--exec-dryrun` in `koa2-stage-client`.
`koa2_handoff_stage.ko` changed since the 2026-11-26 kit: only one function, `koa2_quiesce_verify`, differs, from the USB-idle path commit 813dc1d added to it; the import table is unchanged.
Both modules' stock CRC checks are `passed: true`; see [crcs.json](crcs.json) (staging module) and [exec-crcs.json](exec-crcs.json) (execute module).
The 16 files' checksums are in `readom-koa2-stage-test/SHA256SUMS`, and the whole folder's in [SHA256SUMS](SHA256SUMS). The host tests `koa2-exec`, `koa2-exec-wiring` and `koa2-exec-dryrun` cover every refusal, every step failing and every restore failing; `koa2-jump-baremetal` ran the real trampoline on QEMU's Cortex-A7.
For the design see [the handoff implementation](../../../../boot/koa2/README.en.md); for the full procedure see section 8d of [the device runbook](../../device-runbook.en.md).
