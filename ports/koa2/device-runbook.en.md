# Device test procedure (Oasis 2)

This file is for **whoever runs the test**, human or agent, and assumes no
earlier conversation. Work through it in order; each step says what it needs,
what it gives back, and what to do when it goes wrong. Chinese version:
[device-runbook.md](device-runbook.md).

**What this run does**: loads the staging module into the stock kernel, has it
check every preparation the handoff needs on the device itself, and unloads it.
**It does not jump to the new kernel**; no code in the module can.

**What it does not do**: no partition write, no bootloader change, no IDME, no
calibration data, no network, no intentional reboot; a module fault may still hang or restart the kernel. Everything it writes is under
`koa2-stage-test/` on the Kindle.

**Status (2026-11-21)**: this procedure has already passed on the device. Do not repeat it merely to continue on another computer; see [CONTINUE.en.md](CONTINUE.en.md). Use it again only when a changed staging kit needs a new device check.

## 0. Confirm these first

| Needs | How to confirm | If not |
|---|---|---|
| The Kindle is an Oasis 2, firmware 5.16.2.1.1 | `/etc/prettyversion.txt` | Stop. The kit accepts this firmware only |
| The kernel is `4.1.15-lab126` | `uname -a` | Stop. The script refuses it as well |
| Jailbroken, with KUAL installed | The KUAL menu appears | Stop; deal with the jailbreak first |
| Battery over 50% | The device status bar | Charge first. Do not load the experimental module with a low battery |
| The build host is WSL `Readom-KOA2` | `wsl.exe -l -v` | See [build-environment.md](build-environment.md) |

## 1. Build the kit on the host

Inside WSL (never through Windows: company DLP encrypts `.py` and `.json`
written to a Windows disk):

Prerequisites: the headless-smoke source and output trees and ARM toolchains described in [build-environment.md](build-environment.md) must already exist. A Git clone alone cannot build this kit.

```bash
set -e
cd ~/readom-work/readom
bash ports/koa2/build-smoke-initramfs.sh
bash ports/koa2/build-stage-test-kit.sh
```

The result is in `~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/`:
`stage-test.sh`, `koa2_handoff_stage.ko`, `koa2-stage-client`, `zImage`,
`initramfs.cpio.gz`, `SHA256SUMS`, `menu.json`, `config.xml`.

Before exporting, the script refuses a module that needs a GOT, imports a
symbol the tree it was built against does not export, writes a coprocessor
register, imports `cpu_down` or anything that restarts, or carries a zImage
that is not the one it pinned. **If it refuses, do not copy anything to the
device**; fix it first.

## 2. Copy it to the Kindle

With USB connected, mount and copy from inside WSL as root:

The example uses D:, the observed Kindle drive, not a fixed assignment. Confirm it in Windows first. Run the block in a dedicated shell: it stops on errors and refuses an existing extension directory. Preserve an old kit before replacing it.

```bash
set -e
# Confirm the Kindle drive in Windows first. D: was used on 2026-11-21.
sudo mkdir -p /mnt/kindle
sudo mount -t drvfs D: /mnt/kindle
test -d /mnt/kindle/documents && test -d /mnt/kindle/extensions
test ! -e /mnt/kindle/extensions/readom-koa2-stage-test
cp -r ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test /mnt/kindle/extensions/
```

Then check this one thing, to be sure DLP did not encrypt the JSON:

```bash
cd /mnt/kindle/extensions/readom-koa2-stage-test
sha256sum -c SHA256SUMS
cmp ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/menu.json menu.json
cmp ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/config.xml config.xml
head -c 8 menu.json
cd ~
sync
sudo umount /mnt/kindle
```

It has to print the start of a JSON object. If it prints `%TSD-Head` the file
was encrypted, KUAL will not read the menu, and the copy has to be made another
way (not through Windows Explorer). A changed `.ko`, zImage or script is caught
by the checksum test on the device, but `menu.json` and `config.xml` are not in
that list, so compare both menu files against the source as shown above.

Eject the USB volume in Windows before touching the Kindle.

## 3. Run it on the Kindle

KUAL, then **Readom KOA2 Stage Test**, then **Stage and check handoff (no
jump)**.

It takes under a minute. The screen will not change, which is expected: the
test does not touch the display.

In order, the script checks the kernel identity and its own checksums, copies
the stock `.ko` files read-only, records `panic_on_oops`, memory and dmesg,
runs `insmod`, stages the zImage, the runtime FDT and the initramfs, seals,
runs `rmmod`, and records everything again.

## 4. Read the result

The report is in `koa2-stage-test/<UTC time>-<PID>/` on the Kindle, and
`LAST_RUN.txt` names the newest. Read `result=` at the end of `report.txt`:

| result | Meaning | Next |
|---|---|---|
| `insmod_refused` | Loading failed; inspect dmesg for CRC, identity or other errors. This kit loaded successfully in the recorded run | Section 6 |
| `sealed_plan_valid` | Staging and sealing passed; also confirm rmmod_exit=0 | Go to section 7 |
| `seal_failed` | The module loaded but sealing was refused | Read `seal_stage` against section 5 |
| `refused_unknown_kernel` | The kernel is not `4.1.15-lab126` | Check the device and firmware. Nothing was loaded |
| `refused_kit_damaged` | The kit's checksums do not match | Redo sections 1 and 2 |
| `refused_module_already_loaded` | The module is already in the kernel | `rmmod koa2_handoff_stage` first |

The STATUS lines before `client_exit=0` carry the numbers:

| Field | Should be |
|---|---|
| `state` | 2 (sealed) |
| `plan_valid` | 1 |
| `execute_available` | **always 0**. There is no jump at this stage; a 1 means the wrong thing was installed |
| `fdt_bytes` | about 47900 (the rewritten FDT, smaller than the stock one) |
| `payload_pages` | 1706 in the recorded run; depends on payload sizes (the pages the three payloads hold) |
| `collision_pages` | pages set aside, 0 to 8192. Past 8192 it fails with `-ENOSPC` |
| `list_pages`/`list_entries` | the copy list's size; `list_entries` is about `payload_pages + 3 + list_pages` |

## 5. What seal_stage means

| seal_stage | Where it stopped |
|---|---|
| 1 | A payload was not written in full |
| 2 | A payload's hash is not what was declared (`seal_result` is the slot: 0 zImage, 1 FDT, 2 initramfs) |
| 3 | The zImage is not the one the build pinned |
| 4 | The stock runtime FDT is malformed, or not a Zelda |
| 5 | The FDT reserves something the measured exclusions do not cover (**this device's reserved memory differs from the record; important**) |
| 6 | Rewriting the FDT failed |
| 7 | `/memory` is not 0x80000000 + 0x20000000 |
| 8 | The layout check failed |
| 9 | A staged page lies on a destination |
| 10 | The copy-list audit failed |

Any of 5, 7, 9 or 10 means stop and work out why rather than retry: each says
this device's memory layout is not the one
[boot-preflight-findings.md](boot-preflight-findings.md) recorded.

## 6. If it was insmod_refused

Copy the report back to the host and check the CRCs:

```bash
set -e
# Use the actual drive letter; mount only if not already mounted.
sudo mkdir -p /mnt/kindle
sudo mount -t drvfs D: /mnt/kindle
mkdir -p ~/koa2-evidence/stage-test
cp -a /mnt/kindle/koa2-stage-test/. ~/koa2-evidence/stage-test/
sudo umount /mnt/kindle
```

```bash
cd ~/readom-work/readom && R=~/koa2-evidence/stage-test/$(cat ~/koa2-evidence/stage-test/LAST_RUN.txt) && python3 ports/koa2/module-crcs.py check --stock $R/stock-modules --symvers ~/readom-work/out-headless-smoke/Module.symvers ~/artifacts/koa2-handoff-module/koa2_handoff_stage.ko
```

`differ` lists symbols whose CRCs disagree and `unknown` those no stock module
records. If both are empty and `passed` is true the module should have loaded,
so the reason is in the dmesg around `insmod` instead.

When something disagrees, follow "the stock kernel's version checks" in
[boot/koa2/README.md](../../boot/koa2/README.en.md): replace the import, or
rebuild the types those symbols involve with the stock configuration. Then redo
section 1, giving the build the stock modules so it checks itself first:

```bash
KOA2_STOCK_MODULES=$R/stock-modules bash ports/koa2/build-stage-test-kit.sh
```

Then return to section 2.

## 7. If it was sealed_plan_valid

This kit passed its staging checks, not the complete handoff. Confirm client_exit=0, rmmod_exit=0 and execute_available=0. Copy the
report back (the commands in section 6), update [STATUS.md](STATUS.en.md), and
**stop here**.

The next stage is the handoff itself, and two pieces of it do not exist yet,
neither of them part of this procedure: the hardware run/undo bodies of the
quiesce sequence ([quiesce.c](../../boot/koa2/quiesce.c) currently holds only
the order, which is what the host tests check), and reading the EPDC
framebuffer addresses during the handoff so they can be excluded.
The trampoline ([jump.S](../../boot/koa2/jump.S)) is written and has run
bare-metal on an emulated Cortex-A7, but it is in no module and has never run
on the device.

## 8. When something goes wrong

| What you see | Why | What to do |
|---|---|---|
| The device hangs or restarts after `insmod` | The module faulted in the stock kernel (an oops) | Wait. With `panic_on_oops` at 1 the stock kernel restarts itself into stock after about 5 seconds; at 0 the process may die, but the kernel may also remain unusable |
| The screen does nothing but the device is alive | The test does not touch the display; expected | Wait for the script to end, then read the report |
| Nothing responds at all | Unknown | Hold power about 40 seconds to force a restart. Do not rely on a 60-second reset: a running kernel may keep feeding the watchdog |
| Worried about data after a restart | No partition was ever written | Stock is expected to boot; check the library, KOReader and KUAL after recovery |

A restart through the stock path writes printk to eMMC sector 0x9a400, as the
stock kernel does for any crash. It has nothing to do with the handoff; see the
last section of [boot-preflight-findings.md](boot-preflight-findings.md).

## 8b. The quiesce dry run (the next thing to do on the device)

The staging test has passed, so this is the next step on the device: **stop
what can be stopped, look at it, and put it all back**. KUAL, then Readom KOA2
Stage Test, then **Quiesce dry run (stops devices, restores them)**.

It loads no handoff module, copies no payload and jumps nowhere. The stock
system is running normally again when it finishes.

In order it stops the EPDC (the screen keeps its last image, which is
expected), Wi-Fi, the USB gadget, and touch and the sensors; then syncs and
remounts the rootfs read-only; then takes the second core down. Having checked
the result, it puts everything back in reverse.

The report is `koa2-quiesce-dryrun/<UTC time>-<PID>/report.txt`; read `result=`
at the end:

| result | Meaning | Next |
|---|---|---|
| `quiesced_and_restored` | The path works: all four masters stopped, the rootfs went read-only, the second core went down, and all of it came back | Copy the report to the host, look at the timings, and only then discuss the handoff itself |
| `incomplete_see_above` | Something did not stop. `IN_USE=`, `REMOVE_FAILED=` or `ROOTFS_REMOUNT_FAILED` in the report says which | Stop and work out why. **Do not retry**: if a driver cannot be stopped, the handoff cannot be done |
| `refused_unknown_kernel` | The kernel is not `4.1.15-lab126` | Nothing was touched |

Whether it succeeds or fails, the script puts back what it stopped (the
`[restore]` section at the end of the report), in this order: the rootfs goes
read-write first, then the drivers are reloaded, then the second core comes
back online. If a `RELOAD_FAILED=` or `CPU1_STILL_DOWN` does appear, restarting
the device fixes all of it: no partition was ever written.

Wi-Fi and touch will be gone for a few seconds, which is expected. The screen
stops refreshing once the EPDC is stopped, until it is put back.

## 8c. The marker test (place one, restart, see that it is still there)

This takes two runs, because what is being tested is whether a marker survives a
reset. A marker cannot be read back in the boot that wrote it: `/proc/printkbuf`
publishes the **previous** boot's buffer.

It does not jump, copy a payload, write a partition or take a core down. What it
costs is the last 256 bytes of one boot's kernel log. Section four of
[recovery.md](recovery.en.md) says why it is needed.

**Step one.** KUAL -> Readom KOA2 Stage Test -> **Marker test: place one (no jump)**.
The report is `koa2-marker-test/<UTC time>-<PID>/report.txt`:

| result | What it means | What to do |
|---|---|---|
| `marker_placed` | The marker was written into the printk buffer and cleaned out of the caches | Restart the device and do step two |
| `mark_failed` | The module refused to write it (`client_exit` is not 0) | Stop and read the report; do not retry |
| `insmod_refused` | The module did not load, which has nothing to do with the marker | Section 6 |
| `refused_unknown_kernel` | The kernel is not `4.1.15-lab126` | Nothing was touched |

**Step two: restart the Kindle**, then KUAL -> **Marker test: check after restart**.
An ordinary restart is enough; there is no need to pull power or wait for the watchdog.

| result | What it means | What it tells you |
|---|---|---|
| `marker_survived` | The previous boot's buffer holds the `KOA2_HANDOFF` line | The marker path works, and section two of [recovery.md](recovery.en.md) (the reset preserves DRAM) is confirmed on the device |
| `marker_absent` | Not found | **Stop.** Either the marker never reached DRAM, or the reset does not preserve it -- and the second of those means the reasoning in section two is wrong, which matters far more than the marker does. Do not push towards a jump on this |
| `refused_no_printkbuf` | There is no `/proc/printkbuf` | The kernel has no `CONFIG_LAB126_PRINTK_BUFFER`, so the whole log route does not hold |

Step two keeps only the lines containing `KOA2_HANDOFF` in its report. The
previous boot's full log is the device's own: it is read, then deleted, and never
goes into Git.

## 9. Still needs the device, but not in this procedure

| What | Why it needs hardware |
|---|---|
| The speed mode and tuning the standard sdhci driver reaches on this eMMC | Only measurable on the device |
| The EPDC framebuffer addresses of a given boot | Allocated dynamically; must be read that time |
| What each quiesce step does to the hardware | Only the order and the rollback are checked today |
| Whether the printk buffer survives a WDOG_B reset | Only measurable on the device |
| The handoff itself (copy and jump) | Not until all of the above is settled |
