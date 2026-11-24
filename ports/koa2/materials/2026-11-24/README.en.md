# Two things to test at home (2026-11-24 kit)

This file is the operating sheet. Work through it in order with the Kindle; neither WSL nor a rebuild is needed. Chinese: [README.md](README.md).

| # | Test | The question it answers | What it changes |
|---|---|---|---|
| 1 | Inspect EPDC framebuffer users | Who holds the EPDC driver's 3 references | Read-only, stops nothing |
| 2 | Touch only: sleep and wake controller | After the stock driver puts touch to sleep, is the I²C bus really quiet, and does touch come back after waking | Touch stops for about 12 seconds; the script wakes it |

Neither test loads the handoff module, jumps, writes a partition or unloads a driver. They write only to the Kindle's `koa2-quiesce-dryrun/` and `koa2-touch-quiesce/` folders.

## 0. Check first

Oasis 2, firmware 5.16.2.1.1, jailbroken with KUAL installed, battery above 50%. Otherwise stop; the scripts also refuse any other kernel.

## 1. Copy the kit to the Kindle

Download only this folder:

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-24
git checkout
```

If an older `readom-koa2-stage-test` is still in `extensions/`, move it out first. Copy the whole `ports/koa2/materials/2026-11-24/readom-koa2-stage-test` folder into the Kindle's `extensions/`, then check it on the Kindle drive (use the actual drive letter):

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

Continue only if all 11 lines say `OK` and the second command prints `{"items"`. `%TSD-Head` means the company DLP encrypted the file; copy from the home computer instead. Safely eject USB.

## 2. EPDC framebuffer users (first; USB may stay connected)

KUAL → **Readom KOA2 Stage Test** → **Inspect EPDC framebuffer users**. It takes a few seconds and the screen does not change.

The report is `koa2-quiesce-dryrun/<run ID>/report.txt`; its last line is `result=quiesce_blocked_pending_restore` (expected). Read the `[epdc_users]` section:

| `epdc_reference_accounting` | Meaning |
|---|---|
| `matched` | The listed processes account for every reference. Note the `epdc_user_pid=… comm=…` lines |
| `shared_files_possible` | More processes than references, most likely sharing one open file after fork |
| `more_references_than_openers` | Some reference is not a userspace open and the source does not explain it; bring the report back as is |
| `unavailable` | No framebuffer node was found; bring the report back |

## 3. Touch-only sleep and wake

Unplug USB, stay in the stock system with the screen on, and **do not press power or lock the screen during the test**.

KUAL → **Touch only: sleep and wake controller (tap after)**. Touch does not respond for the first 12 seconds or so, as expected; then tap the screen once within ten seconds.

The report is `koa2-touch-quiesce/<run ID>/report.txt`:

| result | Meaning | Then |
|---|---|---|
| `touch_quiesce_restored_touch_observed` | **The wanted result**: bus and touch interrupts were 0 while asleep, power was off, and a tap arrived after waking | Bring the report back |
| `touch_quiesce_restored_touch_unconfirmed` | Quiet held, but no tap arrived within ten seconds of waking | Tap the stock UI to confirm touch works; bring the report back |
| `touch_wake_failed_restart_required` | The wake did not succeed | Restart normally once and touch returns. Do not retry |
| any other `touch_*` | The report names the failed condition (bus not quiet, power not off, screen locked, and so on); a wake was attempted | Confirm touch works, bring the report back, do not retry |
| `refused_*` | A precondition failed and nothing was written | Bring the report back |

If touch still does not respond afterwards, restart normally. If the device does not respond at all, hold power for about 40 seconds.

## 4. Bring the reports back and clean up

```bash
cp -r /d/koa2-quiesce-dryrun /d/koa2-touch-quiesce ~/koa2-evidence/
```

Once the copies open and are complete on the computer, delete `extensions/readom-koa2-stage-test` and both report folders from the Kindle. The reports hold only the kernel identity, process names, counts and states, with no coordinates, arguments or screen content; the raw reports stay local and do not go into Git.

## 5. Where this kit came from

Built in WSL from branch commit `8114e3e` (`ports/koa2/build-stage-test-kit.sh`). The 11 file hashes are in `readom-koa2-stage-test/SHA256SUMS`, and the device scripts check them again before running. All 35 symbols the module imports match the stock CRCs (`crcs.json`); neither test loads it. Host tests: 22 passed, 0 failed. Design and source evidence are in the last two sections of the [quiesce audit](../../handoff-quiesce.md); the full procedure is in the [device runbook](../../device-runbook.en.md).
