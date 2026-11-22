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

The first two are done, and not the way they were planned. Section six of
[handoff-quiesce.md](handoff-quiesce.md) says why:

- **The devices are not stopped from the module.** Almost nothing that stops one
  can be checked (`lock_fb_info`'s CRC differs from stock, so touching the
  framebuffer would be refused at load), and once a clock is gated, reading a
  peripheral register hangs the bus -- so "read the engine's status and confirm
  it is idle" would hang the device in exactly the case where quiescing had
  worked. It is done from userspace instead, through the kernel's own removal
  and hotplug paths (`stage-test/quiesce-dryrun.sh`), and the module only
  checks the result (`quiesce-verify.c`).
- **The EPDC exclusion is no longer needed.** The EPDC's remove path frees those
  DMA framebuffers, so once it is unloaded they are ordinary free pages and
  there is nothing to exclude.

What can be done now without the device:

1. ~~Wire the pre-jump marker into the jump stage~~ **done (2026-11-22)**: the write lives in the module's `K2S_MARK` (`koa2_marker_place`), and is not a jump. Wiring it corrected three things section four of [recovery.md](recovery.md) used to say: the marker goes at the **end** of the live half, it has to clean itself out of the caches (a watchdog reset does not), and `__arm_ioremap` refuses RAM, so `phys_to_virt` is used. Two symbol choices there make the module fail to load outright; `koa2-marker-wiring` guards both. Proving on the device that a marker survives a reset takes two runs, see section four below.
2. Investigate the [EPDC source gap](epdc-source-gap.md). This independently blocks full Android and is not solved by passing staging.
3. The jump stage itself (`boot/koa2/jump.S` is written and verified bare-metal in QEMU, but is in no module). Before wiring it in, everything in section five of [recovery.md](recovery.md) has to hold.
4. Run the tests below for any change. Record passes, failures and skips separately; missing dependencies are not passes.

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

See [the build environment](build-environment.md). Kernel sources, cross toolchains, build outputs and the WSL virtual disk do not arrive with a Git pull.
Keep the staging and execution entry points separate; do not simply make the staging module jump.

## 4. When the Kindle is needed again

The next trip to the device has two things on it, in either order, and neither
touches the jump:

- **The marker test**, section 8c of [the device procedure](device-runbook.en.md):
  place a marker, restart, then read `/proc/printkbuf` to see that it is still
  there. It also tests what section two of [recovery.md](recovery.en.md) only
  reasons about from source -- that the reset preserves DRAM. If the marker does
  not survive, that section is wrong.
- **The quiesce dry run**, section 8b: stop the devices and put them all back,
  loading no module and jumping nowhere.

Before a real handoff, recovery, log collection and the stop conditions at each
step have to be stated -- the recovery analysis is written
([recovery.md](recovery.en.md)), and the two conditions left are both on the
device.
The user starts device tests by hand; no repeat hardware collection or staging run is needed now.
See [the device procedure](device-runbook.en.md) and [stage gates](ROADMAP.en.md).

## 5. Where the materials are now

At the user's request, the [materials folder](materials/2026-11-21/README.en.md) is committed to this branch: stock modules, de-identified complete dmesg, toolchain, kit, source archive parts and reference configuration arrive with Git.
Its README includes SHA256 checks, source assembly and rebuilding commands.
The stock runtime DTB is the only missing item; no separate backup was found, and the built DTB is not a substitute.
Local raw logs remain unchanged; uploaded logs replace device and network identifiers while preserving the remaining sequence.
