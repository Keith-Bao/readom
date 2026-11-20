# Oasis 2 porting workspace

The workspace for making an Oasis 2 (firmware 5.16.2.1.1, kernel
4.1.15-lab126) keep its stock system and also boot Android 8.1.
Chinese: [README.md](README.md).

**Where it stands**: staging and validation have host tests; **device staging passed on 2026-11-21**, and no new
kernel has booted. The plan is [ROADMAP.md](ROADMAP.en.md) and the step-by-step
state is [STATUS.md](STATUS.en.md).

**The PW3 package does not apply here**: that is a different device, on a
3.0.35 kernel with a 3G/1G split.

**Continue without the Kindle**: read [continuing on another computer](CONTINUE.en.md). Device staging passed; hardware quiesce and actual kernel boot are still pending.

## Where to start reading

| What you want to do | Read |
|---|---|
| Run this round's test on the device | [the device runbook](device-runbook.en.md), which carries its own prerequisites, commands and failure handling |
| Understand how the handoff is built | [boot/koa2/README.md](../../boot/koa2/README.en.md) |
| Set up the build environment | [build-environment.md](build-environment.md) |
| See what was measured on this device | [boot-preflight-findings.md](boot-preflight-findings.md), [hardware-findings.md](hardware-findings.md) |
| Know why Android cannot boot yet | [epdc-source-gap.md](epdc-source-gap.md), [android41-audit.md](android41-audit.md) |

## What is established about this device

- Kernel `4.1.15-lab126`, `#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023`, armv7l, both cores online.
- A 2G/2G split: `PAGE_OFFSET` and `PHYS_OFFSET` are both 0x80000000, and all 512 MiB is lowmem, identity mapped.
- Usable RAM in two pieces: 0x80000000–0x9bffffff and 0x9c500000–0x9fffffff; the hole between them is the EPDC's no-map reservation.
- The runtime FDT holds two memreserves (the stock FDT itself and the stock initrd), and `/memory` is 0x80000000 + 0x20000000.
- The eMMC goes through the Falcon BIOS (`falcon_blk`); in source it can be given back to the standard `sdhci-esdhc-imx`, which is unverified on hardware.
- A 60-second watchdog resetting through WDOG_B, and `imx2_wdt` in the stock configuration feeds it from a kernel timer until `/dev/watchdog` is opened.
- Every restart through the stock path writes 256 KiB of printk to eMMC sector 0x9a400.
- The USB volume is about 6.67 GB, and `system/version.txt` reads Kindle 5.16.2.1.1 (409745 002).
- `VERSION` in the root is the jailbreak component's version, not the firmware's.

## What is in this directory

| File | What it is for |
|---|---|
| `device-runbook.en.md` | The device test procedure (this round's work) |
| `build-stage-test-kit.sh` | Builds the device test kit |
| `build-handoff-module.sh` | Builds the staging module and checks it before exporting |
| `build-stock-kernel.sh` | Builds the stock and experimental kernels |
| `derive-kernel-footprint.py` | Measures what the new kernel writes as it decompresses |
| `module-crcs.py` | Says whether a module would pass the stock kernel's version checks |
| `compare-kernel-build.py` | Says whether two builds are the same kernel |
| `stage-test/`, `preflight/`, `kual/` | Three KUAL extensions (the staging test, the read-only report, the hardware report) |
| `smoke-init.c`, `build-smoke-initramfs.sh` | The minimal initramfs's PID 1, and packing it |

The handoff code itself is in [boot/koa2/](../../boot/koa2/). One command runs
every host test:

```bash
python3 tools/run-tests.py --filter koa2
```

## Standing constraints

- Nothing is written to a partition, the bootloader, IDME or calibration data.
- Experiments on the device are started by the user, and only once recovery can be argued.
- Raw device data (the runtime DTB, bootargs, kernel logs, the stock modules) stays on the device and in `~/koa2-evidence/`, never in the repository.
- Company policy and DLP are not worked around: repository files are edited and built inside WSL's ext4 only.

The reviewed short staging report and kit hashes are published under reports/; full raw device evidence stays local.

The user-authorized modules, de-identified logs and build materials are in [the materials folder](materials/2026-11-21/README.en.md); this explicit upload request updates the earlier local-only scope.
