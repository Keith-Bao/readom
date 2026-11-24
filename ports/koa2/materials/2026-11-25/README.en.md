# KOA2 Consolidated Device Test Sheet (2026-11-25)

All host-side development, kernel builds, and 32 KOA2 host tests are complete. This sheet consolidates the remaining device-side checks into one batch. Chinese: [README.md](README.md).

## 0. Preparation

- Oasis 2, firmware `5.16.2.1.1`, jailbroken with KUAL installed, battery > 50%.
- Latest kit built in WSL (`Readom-KOA2`) at `~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test` (13 files pinned in `SHA256SUMS`).
- Copy to `extensions/readom-koa2-stage-test` on the Kindle, verify all 13 SHA256 entries are `OK`, then safely eject and unplug USB.

## 1. Run in order (no raw partition writes, no jump)

| # | KUAL entry | What it verifies | Expected result |
|---|---|---|---|
| 1 | **Inspect EPDC framebuffer users** | Scans both `/etc/upstart/` and `/etc/init/` for `x.conf` `start on`/`stop on` dependencies | `epdc_reference_accounting=matched`, `epdc_upstart_*` |
| 2 | **Combined dry-run: USB + Touch + CPU1 (reversible, no jump)** | Combined USB unload + touch sleep + CPU1 offline with reverse-order restore (EPDC untouched) | `result=full_quiesce_dryrun_restored`, Windows reads Kindle drive on reconnect |

## 2. Collect reports and clean up

Reconnect to the PC, copy `koa2-quiesce-dryrun/` and `koa2-full-quiesce-dryrun/` to local `F:\Kindle\koa2-evidence\`, verify contents, and remove `extensions/readom-koa2-stage-test` plus both report folders from the Kindle. Keep raw reports local; do not commit them to Git.

## 3. Next hardware gates

1. Use the `x.conf` dependencies from step 1 to run isolated `epdc-quiesce.sh` stop/reload verification.
2. Once all peripherals pass reversible quiesce, proceed to the first watchdog-guarded `headless-smoke` jump (see [recovery.md](../../recovery.en.md)).
