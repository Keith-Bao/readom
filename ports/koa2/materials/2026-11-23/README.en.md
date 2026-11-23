# The two things to test on the device (2026-11-23 kit)

This file is the operating sheet for the evening. Hold the Kindle and work
through it in order; no other document is needed, and neither WSL nor a rebuild.
Chinese: [README.md](README.md).

**What is being tested**: two things, in this order (the marker test first, then
the quiesce dry run), about 15 minutes together.

| # | Test | The question it answers | What it costs |
|---|---|---|---|
| 1 | The marker test (place one, restart, check) | Whether DRAM is still there after a reset. It is the one conclusion in section two of [recovery.md](../../recovery.en.md) that is still only reasoned from source | The last 256 bytes of one boot's kernel log |
| 2 | The quiesce dry run | Whether every device that touches memory can be stopped and then put back | The screen, Wi-Fi and touch stop for a dozen seconds; the script puts them back |

**Neither jumps.** No partition write, no boot change, no IDME, no calibration
data, no network. Everything written goes under `koa2-marker-test/` and
`koa2-quiesce-dryrun/` on the Kindle. The jump trampoline
(`boot/koa2/jump.S`) is compiled into nothing in this kit.

## 0. Confirm these first

| Precondition | How | If not |
|---|---|---|
| An Oasis 2 on firmware 5.16.2.1.1 | In Settings, or `/etc/prettyversion.txt` | Stop. The kit knows this one firmware and the scripts refuse anything else |
| Jailbroken, with KUAL installed | The KUAL menu appears | Stop |
| Battery above 50% | The status bar | Charge first |

## 1. Copy the kit to the Kindle

The kit is `readom-koa2-stage-test/` in this folder: ten files, nothing to
build, copy it as it is.

**On a home computer** (no company DLP, so Explorer is fine): download this
folder and put the whole `readom-koa2-stage-test` directory into the Kindle's
`extensions/`.

Only this one folder is needed; the branch itself is about 200 MB:

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2 https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-23
git checkout
```

With the Kindle on USB, the destination is
`<Kindle drive>/extensions/readom-koa2-stage-test/`.
**If the previous kit (the 11-21 one) is still there, move it out of
`extensions/` first** -- same directory name, same menu name, and leaving it
makes it impossible to tell which one ran tonight. Anywhere else on the Kindle
under another name will do.

After copying, verify on the Kindle drive that nothing was altered on the way:

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
```

All seven lines must read `OK`. Then check the menu files are plain text
(`menu.json` and `config.xml` are not in the hash table):

```bash
head -c 8 /d/extensions/readom-koa2-stage-test/menu.json
```

It must print `{"items"`. If it prints `%TSD-Head` the company DLP encrypted the
file and KUAL cannot read the menu -- which happens only on the company laptop;
copy from a home computer, or from WSL as section 2 of
[the device procedure](../../device-runbook.en.md) describes.

Eject the USB safely, then work on the Kindle.

## 2. The marker test (do this one first; it needs one restart)

**Why a restart**: what is being tested is whether a marker survives a reset. A
marker cannot be read back in the same boot -- `/proc/printkbuf` publishes the
**previous** boot's buffer. So this is done in two halves.

**First half**: KUAL -> **Readom KOA2 Stage Test** -> **Marker test: place one
(no jump)**. A few seconds. The screen does not change, which is normal: the
test does not touch the display.

The report is `koa2-marker-test/<UTC time>-<PID>/report.txt` on the Kindle:

| result | Meaning | What follows |
|---|---|---|
| `marker_placed` | The marker went into the printk buffer and the cache was written back | Restart the device and do the second half |
| `mark_failed` | The module refused to write it | Stop, keep the report, do not retry |
| `insmod_refused` | The module did not load; nothing to do with the marker | Stop; the dmesg lines in the report are what matters |
| `refused_unknown_kernel` | The kernel is not `4.1.15-lab126` | Nothing was touched; check the model and firmware |

**Second half: restart the Kindle** (a normal restart; no need to pull power or
wait for the watchdog), then KUAL -> **Marker test: check after restart**.

| result | Meaning | What it establishes |
|---|---|---|
| `marker_survived` | The previous boot's buffer has the `KOA2_HANDOFF` line in it | **The result wanted**: the marker route works, and the reset preserving DRAM is confirmed on the device |
| `marker_absent` | The buffer was read (`previous_boot_log_lines` in the report is in the hundreds) but holds no marker | **Stop.** Either the marker never reached DRAM, or the reset does not preserve DRAM; the latter means section two of [recovery.md](../../recovery.en.md) is wrong, which matters far more than the marker |
| `refused_buffer_unreadable` | The buffer was not read at all (`previous_boot_log_lines=0`) | Not a marker problem: the reading itself failed. **Do not** read this as `marker_absent` |
| `refused_no_printkbuf` | There is no `/proc/printkbuf` | The kernel lacks `CONFIG_LAB126_PRINTK_BUFFER` and the whole log route is void |

That last result was added on 2026-11-23. The original script pulled the text
out with `strings`, and nothing says this Kindle's busybox has that applet at
all (only `tr` has been shown to work on it); a missing applet writes nothing,
and the script reported that as `marker_absent` -- precisely the alarming
conclusion, from a tool that never ran. The read now has to prove itself first
(every boot leaves hundreds of timestamped kernel log lines in there), and a
failed read gets its own result.

The second half keeps only the lines containing `KOA2_HANDOFF`. The previous
boot's full log is the device's own: it is deleted after reading and never
enters Git.

## 3. The quiesce dry run (after the marker test)

KUAL -> **Readom KOA2 Stage Test** -> **Quiesce dry run (stops devices, restores
them)**.

It loads no handoff module, copies nothing and jumps nowhere. The stock system
is running normally again when it finishes.

What stops, in order: the EPDC (**the screen keeps its last image, which is
normal**), Wi-Fi, the USB gadget, touch and the sensors; then sync and the
rootfs remounted read-only; then the second core offline. After the check,
everything goes back in reverse. Wi-Fi and touch are gone for a dozen seconds,
as expected. The screen refreshes again only once the EPDC is back.

The report is `koa2-quiesce-dryrun/<UTC time>-<PID>/report.txt`:

| result | Meaning | What follows |
|---|---|---|
| `quiesced_and_restored` | **The result wanted**: all four masters stopped, the rootfs went read-only, the second core went down, and everything was put back | Read the per-step timings in the report |
| `incomplete_see_above` | Something did not stop; `IN_USE=`, `REMOVE_FAILED=` or `ROOTFS_REMOUNT_FAILED` in the report says which | Stop. **Do not retry**: a driver that cannot be stopped means the handoff cannot be done |
| `refused_unknown_kernel` | The kernel is not `4.1.15-lab126` | Nothing was touched |

Whether it succeeds or fails, the script puts back what it stopped (the
`[restore]` section at the end of the report): the rootfs goes read-write
first, then the drivers are reloaded, then the second core comes back online.
If `RELOAD_FAILED=` or `CPU1_STILL_DOWN` appears, **one restart fixes all of
it** -- no partition was ever written.

## 4. If something goes wrong

| What happens | Why | What to do |
|---|---|---|
| It hangs or restarts after `insmod` | The module faulted inside the stock kernel | Wait. With `panic_on_oops` at 0 it may only kill the process; this device reports `panic=5`, so a panic restarts into stock after about five seconds |
| The screen does nothing but the device is alive | The marker test never touches the display; the dry run stopped the EPDC | Normal, wait for the script to finish |
| Completely unresponsive | -- | Hold power for about 40 seconds. Do not count on the 60-second watchdog: a kernel still running may keep feeding it |
| Worried about data after a restart | No partition was written | It should boot the stock path; check the library and KUAL afterwards |

## 5. Bring the reports back

Copy the two directories to the computer rather than reading one line off the
screen:

```bash
cp -r /d/koa2-marker-test /d/koa2-quiesce-dryrun ~/koa2-evidence/
```

The reports hold the kernel identity, the kit hashes, the test state and the
timings. The marker test's report keeps only the `KOA2_HANDOFF` lines; the
previous boot's full log is deleted on the device after reading. To upload
anything, de-identify it the way the
[materials folder](../2026-11-21/README.en.md) describes; the raw reports stay
local and out of Git.

## 6. What this kit is, and is not

| | |
|---|---|
| Is | Two read-only-natured tests for a jailbroken Oasis 2 on firmware 5.16.2.1.1 |
| Is not | An Android package. This branch has no working KOA2 Android image |
| Is not | A jump. `execute_available` is always 0 and the trampoline is not compiled in |

The `zImage` and `initramfs.cpio.gz` in the kit are the payload the marker test
stages (the module checks their hashes). Neither is booted.

## 7. Where this kit came from

| File | SHA256 |
|---|---|
| `koa2_handoff_stage.ko` | `0699bef9d0a2…` |
| `koa2-stage-client` | `a39bbaf2cb35…` |
| `zImage` | `4921c4b2b1dc…` |
| `initramfs.cpio.gz` | `3aafcaf8924a…` |

The full sums are in `readom-koa2-stage-test/SHA256SUMS`, which the device
scripts check themselves before each run.

The module is compiled against the `out-module-abi` tree, and **all 35** symbols
it imports carry the same CRCs as stock (`crcs.json`; the reasoning is in
[the stock module ABI](../../stock-module-abi.en.md)). The build script's three
gates all passed: no GOT, no coprocessor register write, and no import of
`cpu_down` or anything like it. The `.ko` hash differs from the 11-21 one while
its size and imports are identical -- the build happens in a `mktemp`
directory whose name lands in the debug paths, which is not a code change.

The host tests pass 18 and fail 0 (`python3 tools/run-tests.py --filter koa2`).
One of them, `koa2-marker-check`, was added on 2026-11-23 to guard the false
reading described in section 2.

The previous device test (2026-11-21, staging check `sealed_plan_valid`) and its
evidence are in
[reports/2026-11-21-stage-test](../../reports/2026-11-21-stage-test/README.md).
Tonight does not repeat the staging test: the module was rebuilt but its CRCs
and imports did not change, and the marker test does an `insmod` of its own,
which reports `insmod_refused` if it cannot load.

What comes after these two is in [where the port stands](../../STATUS.en.md) and
[continuing on another computer](../../CONTINUE.en.md); the full device
procedure is [the device runbook](../../device-runbook.en.md), of which this
file is the operating sheet for sections 8b and 8c.
