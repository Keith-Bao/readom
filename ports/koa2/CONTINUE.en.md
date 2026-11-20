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

1. Audit and implement hardware run/undo in the order in boot/koa2/quiesce.h. Define entry points, completion conditions, timeouts and recovery. quiesce.c currently holds only the ordering and rollback framework.
2. Design the path to read and exclude live EPDC framebuffer addresses. Historical addresses are not constants; real values and hardware stop behaviour need later device validation.
3. Add relevant host failure-path tests and run the KOA2 tests below. Record passes, failures and skips separately; missing dependencies are not passes.
4. Investigate the [EPDC source gap](epdc-source-gap.md). This independently blocks full Android and is not solved by passing staging.

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

See [the build environment](build-environment.md). Kernel sources, cross toolchains, build outputs and the WSL virtual disk do not arrive with a Git pull.
Keep the staging and execution entry points separate; do not simply make the staging module jump.

## 4. When the Kindle is needed again

Once hardware run/undo and live exclusions are implemented and host checks pass, prepare a narrowly scoped device test.
Before a real handoff, define recovery, log collection and stop conditions at each step.
The user starts device tests by hand; no repeat hardware collection or staging run is needed now.
See [the device procedure](device-runbook.en.md) and [stage gates](ROADMAP.en.md).

## 5. Where the materials are now

At the user's request, the [materials folder](materials/2026-11-21/README.en.md) is committed to this branch: stock modules, de-identified complete dmesg, toolchain, kit, source archive parts and reference configuration arrive with Git.
Its README includes SHA256 checks, source assembly and rebuilding commands.
The stock runtime DTB is the only missing item; no separate backup was found, and the built DTB is not a substitute.
Local raw logs remain unchanged; uploaded logs replace device and network identifiers while preserving the remaining sequence.
