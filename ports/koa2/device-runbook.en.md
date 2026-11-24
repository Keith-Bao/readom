# Device test procedure (Oasis 2)

> On 2026-11-23, touch did not recover after the dry run, USB gadget reload failed, and the EPDC driver stayed busy. Do not run the original dry run or attempt a handoff. The updated **Quiesce preflight (read-only)** KUAL entry records driver, input, USB, root filesystem, and CPU state. Its expected result is `quiesce_blocked_pending_restore`. The stop steps below describe the original test for interpreting its reports.

The updated kit also has **Check live touch (tap screen)**. Tap once within ten seconds. The probe opens the current `cyttsp5_mt` event device read-only. `touch_contact_observed` records a complete contact frame, and a timeout means that no contact was observed in that window. The probe saves no coordinates. The preflight still ends with `quiesce_blocked_pending_restore`. The USB section records the stock storage module's LUN parameters and USB controller state. Verify host detection and readable storage separately.

After restarting from a dry run, use **Read previous boot recovery log** to check the previous boot's kernel log. The report keeps only the categories and timestamps of recognized touch and USB messages. Collect it before restarting again because the buffer covers only the previous boot. If an error is absent, check that the buffer was readable and covers the failed boot.

**Isolated USB reload.** Safely eject and unplug USB, then run **USB only: reload storage (unplug USB first)** in KUAL. The script unloads only `g_mass_storage` when the stock kernel, idle LUN, module and disconnected UDC all match. It preserves the dependencies and reloads with the stock parameters. Wait about 30 seconds before reconnecting. The report is `koa2-usb-reload/<UTC time>-<PID>/report.txt`. The isolated USB test passes only when the report says `usb_idle_rebound_host_check_pending` and the host can read the Kindle volume again. If the host still cannot see it after 30 seconds, restart to restore stock USB, save the report, and stop.

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

## Stock touchscreen sleep and wake check

Safely eject and unplug USB, staying in the stock system. In KUAL under Readom KOA2 Stage Test, choose **Stock sleep: record before**. Wait a few seconds, press the power button to lock normally, leave it unplugged for about 30 minutes, then press power to wake. In KUAL choose **Stock sleep: check after (tap screen)**, tap within ten seconds and check that the stock UI responds. Reconnect after the check. Both steps update the same `koa2-quiesce-dryrun/<run ID>/report.txt`.

A valid report requires the same boot, at least one extra second of cumulative system suspension, matching touch node inode and Xorg reader marker, and a complete contact frame. `stock_sleep_not_observed` means system suspension was not demonstrated. The matching node and reader are observations at two points in time. I²C quiescence still requires independent evidence.

### Device cleanup after a test

Keep only the current batch's entries on the Kindle. After each test, copy its report to the computer and verify the copy before removing the device entry, its dedicated files and its local report. Keep shared files used by pending tests.

## Touch-only sleep and wake

Run this only after the host tests pass, on the stock system with USB unplugged and the screen on. In KUAL under Readom KOA2 Stage Test, choose **Touch only: sleep and wake controller (tap after)**. After about 12 seconds the script wakes touch; tap the screen within ten seconds. The report is `koa2-touch-quiesce/<run ID>/report.txt`.

The script keeps the driver loaded and writes only `4` (`cyttsp5_core_sleep`) and `5` (wake) to the stock `drv_debug`. The touch controller is the only device on i2c2, so that bus's interrupt count stands for touch I²C traffic. A 5-second awake window comes first: the watchdog talks to the controller every 2 seconds, so the count must rise or the test refuses. In the same window after sleep, i2c2 and touch interrupts must both be 0, `MAX77796-LSW1` must read `disabled`, the `touch_1v8` GPIO `out lo`, the IRQ thread `S`, and the driver log must show exactly one `Suspend succeeded`. `sleep_status=off` is recorded but never relied on. A system suspend or screen change during the test voids the result.

`touch_quiesce_restored_touch_observed` is the only full pass. `touch_wake_failed_restart_required` needs a normal restart to bring touch back. Every other `touch_*` and `refused_*` result names the condition that failed, and a wake was attempted in each. This test does not change the handoff check: `quiesce-verify.c` still requires the touch module to be unloaded.

## EPDC framebuffer users (read-only)

In KUAL under Readom KOA2 Stage Test, choose **Inspect EPDC framebuffer users**. The report goes to `koa2-quiesce-dryrun/<run ID>/report.txt`; its `[epdc_users]` section lists the module reference count and each process's descriptor and mapping counts, and the last line is still `quiesce_blocked_pending_restore`. USB can stay connected and no driver is stopped.
