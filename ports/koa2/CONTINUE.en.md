# Continue the KOA2 port on another computer

As of 2026-11-21, staging passed inside the stock Oasis 2 system; **host development can continue without the Kindle**.
There is no usable KOA2 Android install package, and no new kernel has booted on hardware.
Chinese: [CONTINUE.md](CONTINUE.md).

## 1. Get the code and confirm the workspace

Work inside WSL's Linux filesystem. Check an existing checkout before updating:

```bash
cd ~/readom-work/readom
git status --short --branch
git fetch origin
git switch port/koa2-quiesce-preflight
git pull --ff-only
```

For a first checkout, clone Keith-Bao/readom inside WSL and select port/koa2-quiesce-preflight.
If status shows local edits or pull refuses, preserve and inspect them; do not reset or overwrite them.
The workspace path is a convention and can differ between computers.
Read [current status](STATUS.en.md), [the handoff implementation](../../boot/koa2/README.en.md) and [the quiesce audit](handoff-quiesce.md).

## 2. What has been verified

| Layer | Evidence | Does not prove |
|---|---|---|
| Host tests | Contract, layout, FDT, copy list, quiesce order and failure rollback | That stock peripherals stop or recover |
| Bare-metal QEMU | The trampoline follows its contract on Cortex-A7 | Real i.MX7D peripherals or a complete device boot |
| Kindle staging | Module loaded, sealed and unloaded, plan_valid=1 | Payload copying, a jump, a new kernel or Android boot |

The [device report](reports/2026-11-21-stage-test/report.txt) records result=sealed_plan_valid,
execute_available=0 and insmod_exit/client_exit/rmmod_exit all 0.
The accompanying [SHA256SUMS](reports/2026-11-21-stage-test/SHA256SUMS) identifies this kit; rebuilds can have different hashes.

## 3. Work that needs no device

1. **Touch I²C.** Trace `cyttsp5_core_sleep_()`, IRQ handling, the watchdog and startup work in verified stock sources. Design a reversible way to drain in-flight IRQ/I²C activity and confirm quiescence without losing evdev or Xorg. Cover power-off failures, timeouts and rollback. Do not relax `quiesce-verify.c` based on `sleep_status=off` or normal suspend.
2. **EPDC.** Use the [source gap audit](epdc-source-gap.md) and stock logs to design read-only observations of framebuffer users, driver references and pending updates. The module still had three references. Establish how to release those and handle DMA buffers before testing an isolated stop.
3. **Handoff checks.** Keep staging separate from execution. Review recovery and early logging in [recovery.md](recovery.en.md). The bare-metal QEMU test lacks serial output on both this work and its baseline. Diagnose the emulator and keep it marked as failing.
4. Run the host tests below and record passes, failures and skips separately.

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

See [the build environment](build-environment.md). Kernel sources, cross toolchains, build outputs and the WSL virtual disk do not arrive with a Git pull.

## 4. When the Kindle is needed again

Staging passed, the marker survived a normal restart, and Windows could read the Kindle after the isolated USB reload. The 2026-11-24 stock sleep report returned `stock_sleep_touch_observed` with two successful suspends, zero failures, and 3,602,010 ms of cumulative sleep. The touch node and Xorg handle markers matched, and the probe received a complete contact frame after wake. The raw report is verified on the home computer, and the device entries and copies have been removed. See the [device results and audit](handoff-quiesce.md).

**No Kindle is needed at work.** Prepare the next device batch after the touch and EPDC designs, host tests and recovery steps are ready. Start with read-only collection and use the results to schedule isolated tests. Verify each copied report before removing its device entry and copy. Keep the full dry run and kernel handoff on hold.

## 5. Where the materials are now

The original kit and its operating sheet are archived in
[the 2026-11-23 materials folder](materials/2026-11-23/README.en.md): the
ten-file KUAL kit, the module's imports and CRC result, and a step-by-step sheet
for the marker test and the quiesce dry run.
At the user's request, the [materials folder](materials/2026-11-21/README.en.md) is committed to this branch: stock modules, de-identified complete dmesg, toolchain, kit, source archive parts and reference configuration arrive with Git.
Its README includes SHA256 checks, source assembly and rebuilding commands.
The stock runtime DTB is the only missing item; no separate backup was found, and the built DTB is not a substitute.
Local raw logs remain unchanged; uploaded logs replace device and network identifiers while preserving the remaining sequence.
