# Continue the KOA2 port on another computer

As of 2026-11-21, staging passed inside the stock Oasis 2 system; **host development can continue without the Kindle**.
There is no usable KOA2 Android install package, and no new kernel has booted on hardware.
Chinese: [CONTINUE.md](CONTINUE.md).

## 1. Get the code and confirm the workspace

Work inside WSL's Linux filesystem. Check an existing checkout before updating:

```bash
cd ~/readom-work/readom
git status --short --branch
git switch port/koa2
git pull --ff-only origin port/koa2
```

For a first checkout, clone Keith-Bao/readom inside WSL and select port/koa2.
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

1. Trace how the stock system initializes and restores touch and USB storage. For touch, verify that `cyttsp5_mt` is registered again and produces events. For USB, verify the stock module parameters and that the host can see storage again. Preserve the root filesystem's original mount mode.
2. Continue investigating the [EPDC source gap](epdc-source-gap.md). EPDC stayed busy during the device dry run, so we have no evidence that its DMA framebuffers can be freed at this stage.
3. Keep staging and execution separate. The handoff gates are in section five of [recovery.md](recovery.en.md).
4. Run the host tests below after changes. Record passes, failures and skips separately.

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

See [the build environment](build-environment.md). Kernel sources, cross toolchains, build outputs and the WSL virtual disk do not arrive with a Git pull.

## 4. When the Kindle is needed again

The restart marker test passed on 2026-11-23. In the original dry run, EPDC stayed busy, USB storage failed to reload, and touch recovered after a forced restart. The replacement KUAL entry is a read-only preflight, which has recorded the stock state after restart.

The Kindle is idle for now. Once the recovery paths and host tests are ready, validate each peripheral separately. The dry run and handoff are on hold. See [the device procedure](device-runbook.en.md) for the KUAL entry.

## 5. Where the materials are now

The original kit and its operating sheet are archived in
[the 2026-11-23 materials folder](materials/2026-11-23/README.en.md): the
ten-file KUAL kit, the module's imports and CRC result, and a step-by-step sheet
for the marker test and the quiesce dry run.
At the user's request, the [materials folder](materials/2026-11-21/README.en.md) is committed to this branch: stock modules, de-identified complete dmesg, toolchain, kit, source archive parts and reference configuration arrive with Git.
Its README includes SHA256 checks, source assembly and rebuilding commands.
The stock runtime DTB is the only missing item; no separate backup was found, and the built DTB is not a substitute.
Local raw logs remain unchanged; uploaded logs replace device and network identifiers while preserving the remaining sequence.
