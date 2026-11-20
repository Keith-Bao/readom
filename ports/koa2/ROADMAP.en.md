# KOA2 porting plan

The goal: an Oasis 2 on firmware 5.16.2.1.1 keeps its stock system and also
runs Android 8.1 and WeChat Read, with touch, the page keys, rotation,
frontlight, network, USB, suspend, and a way back to stock.
Chinese: [ROADMAP.md](ROADMAP.md). Where it stands: [STATUS.md](STATUS.en.md).

## Stages and where each one is

| Stage | What it covers | State |
|---|---|---|
| A Records | Baselines, commands, evidence, next step; one commit per stage | Ongoing |
| B Environment | WSL2 Ubuntu 22.04 `Readom-KOA2`, building inside ext4 | Done, see [build-environment.md](build-environment.md) |
| C Stock kernel | zelda defconfig, zImage, DTB, modules, hashes; clean rebuild | Done (headless-smoke), see [headless-smoke.md](headless-smoke.md) |
| D Android kernel | Patches audited one by one, a 4.1 configuration, Binder contexts first | Audited, not built, see [android41-audit.md](android41-audit.md) |
| E Handoff | Validation, staging, copy list, jump; an SMP/DTB backend | Host side done, device staging passed; no jump executed, see [boot/koa2/README.md](../../boot/koa2/README.en.md) |
| F Minimal Linux | The smoke initramfs, a log path, three round trips | The initramfs is ready; it has never booted on the device |
| G Basic hardware | EPDC v2, touch, page keys, rotation, frontlight | Not started; the EPDC source is missing, see [epdc-source-gap.md](epdc-source-gap.md) |
| H AOSP | koa2-userdebug / Cortex-A7, its own output tree | Not started |
| I Daily use | Wi-Fi, ADB, MTP, WeChat Read, automatic suspend | Not started |
| J Install and roll back | A KUAL entry, model checks, the previous version kept | Not started |

## Inside stage E (where the work is now)

| Piece | State |
|---|---|
| The DTB and memory contract | Done, test `koa2-contract` |
| The measured memory layout and exclusions | Done, see [boot-preflight-findings.md](boot-preflight-findings.md) |
| The kernel's footprint (LZO + AUTO_ZRELADDR) | Done, `ports/koa2/derive-kernel-footprint.py` |
| The layout check | Done, test `koa2-plan` |
| The handoff FDT rewrite | Done, test `koa2-fdt-rewrite` |
| The copy list and its audit | Done, test `koa2-copylist` |
| The staging module (no jump) | Done, test `koa2-stage` |
| Stock symbol CRCs | The tool is done; 63 stock modules recovered, test `koa2-module-crcs` |
| The jump trampoline | Done and run bare-metal, test `koa2-jump-baremetal`; in no module |
| The quiesce sequence | Only the testable order; the hardware bodies are missing, test `koa2-quiesce` |
| The device test kit | Done, device staging passed, see [device-runbook.md](device-runbook.en.md) |

## What each stage has to show before the next

- E to F: `sealed_plan_valid` on the device, and the quiesce hardware bodies written.
- F to G: the user starts it by hand, and it enters the new kernel and returns to stock three times running.
- G to H: the EPDC source gap closed, with refresh, touch, page keys and rotation working.
- H to I: a desktop stable for 30 minutes, with a way back to stock.

## Final acceptance

- 10 boot/return cycles, 2 hours of reading, 500 physical page turns.
- 20 rotations each way, 20 suspend/resume cycles, 8 hours of stock and Android standby compared.
- 10 Wi-Fi reconnections, 10 USB plug and unplug cycles.
- One upgrade, one failed upgrade and one rollback, with the stock library, KOReader and KUAL intact.
- SELinux enforcing, ADB behind authorization, no credentials leaked, every build input with a version and a hash.
- Only then a KOA2 test package, with its own version string and the signing key kept locally.

## How the work is run

Read [STATUS.md](STATUS.en.md) each round, check Git and the real artifacts, finish
and verify the step in hand, keep the evidence, commit and push, and write down
the next step. Host tests, emulator results and device results are recorded
separately. The user does the administrator actions and restarts, the Kindle
boots and recoveries, and the application logins; no cloud resources are bought.
No protective check is deleted, no incompatible module is forced in, and
nothing is written to a raw partition, IDME, a key or calibration data. If the
handoff cannot be shown to be recoverable, work on the device stops, and the
source and the evidence of the blockage are handed over without claiming
completion.
