# The KOA2 handoff

The code that, from inside the stock kernel, puts a new kernel in memory and
jumps to it. It writes no partition and changes no bootloader.
Chinese: [README.md](README.md). The device procedure is
[ports/koa2/device-runbook.md](../../ports/koa2/device-runbook.en.md).

**Where it stands**: everything that stages and checks is written and has host
tests; the jump trampoline is written and has run bare-metal on QEMU's
Cortex-A7, **but it is built into no module**; the quiesce sequence has only
its testable order, and the parts that touch hardware are not written.
**Device staging passed on 2026-11-21; no jump has run on hardware.**

## The order

| Step | What it does | Where | Reversible |
|---|---|---|---|
| 1 | Check the stock kernel's identity | `stage.c` at load | Yes (it does not load) |
| 2 | Stage the zImage, the stock FDT and the initramfs in pages it owns | `stage.c` BEGIN/PUT | Yes (closing frees them) |
| 3 | Check the hashes, the pinned kernel, the FDT's identity and reservations | `stage.c` SEAL, `contract.c` | Yes |
| 4 | Work out the layout and validate it | `plan.c` | Yes |
| 5 | Rewrite the FDT (bootargs, initrd, clear the memreserves) | `fdt-rewrite.c` | Yes |
| 6 | Build the copy list and audit it | `copylist.c` | Yes |
| 7 | Quiesce: EPDC, Wi-Fi, USB, SDMA, eMMC, the second core, interrupts, the watchdog | `quiesce.c` (hardware parts unwritten) | Every step must roll back |
| 8 | Caches off, MMU off, walk the list, jump | `jump.S` (in no module) | **Irreversible** |

Steps 1 to 6 are what the device test does today, and it unloads afterwards.
The point of no return is turning the caches off at the start of step 8: before it, a failure at any
step has to put the stock system back as it was; after it there is no stock
kernel left to return to, and the only paths are the new kernel or the
watchdog.

## How this code is tested

The handoff cannot be verified as a whole on the host, so it is tested in three
layers:

- **Host tests** (`tools/run-tests.py --filter koa2`): the contract, the
  layout, the FDT rewrite, the copy list, the quiesce order, the CRC check, and
  `stage.c` itself against a stand-in for the module API. All under ASan and
  UBSan.
- **Bare-metal QEMU**: `jump.S` really turns off the MMU, walks a list and
  jumps, on a Cortex-A7.
- **The device**: the staging and checking part only, started by the user
  following the runbook.

Booting the whole thing in QEMU does not work, for reasons recorded in
[ports/koa2/STATUS.md](../../ports/koa2/STATUS.en.md): QEMU 6.2's i.MX7D model
has no OCRAM_S, no DDR low-power OCRAM, no GPCv2 core-1 power handshake and no
DDR controller.

## The contract (contract.c)

A read-only check of the DTB's structure, the Zelda identity, and memory
ranges. It refuses truncation, overlapping blocks, an unterminated reserve map,
excessive depth, an invalid token, the wrong compatible, a range that
overflows, and a payload crossing the bounds of allowed RAM. It is not a
replacement for libfdt, and it certifies neither panel calibration nor that a
jump is safe.

The measured layout (`koa2_measured_ram`, `koa2_measured_excluded`) is a fact
about **one device**, not a default; its evidence is in
[ports/koa2/boot-preflight-findings.md](../../ports/koa2/boot-preflight-findings.md).

The host test includes 20000 deterministic mutations; the same test also runs
as a static ARM binary under qemu-arm, and against the device's runtime DTB.

## The layout check (plan.c)

plan.c only decides whether "put the zImage, the FDT and the initramfs here"
holds. It allocates nothing, copies nothing and jumps nowhere. The staged pages
come from the running allocator and are copied to their destinations last, with
the MMU off, so a destination may overwrite the stock kernel, the stock FDT and
the stock initrd, but nothing that has to outlive the jump
(`koa2_handoff_persistent`: the printk buffers, ramoops, the EPDC reservation,
the Falcon BIOS).

What the kernel writes is read out of a build directory by
ports/koa2/derive-kernel-footprint.py (LZO with AUTO_ZRELADDR, so PW3's gzip
script does not apply). For the headless-smoke build: zreladdr 0x80008000, the
page directory from 0x80004000, an Image of 12416856 bytes, _end at 0x80c55a0c
with BSS, and a running zImage using 6722520 bytes past its load address
(image, BSS, stack, 64 KiB of malloc).

The rules:
1. Every segment is page aligned, lies wholly inside one measured RAM interval (all 512 MiB is lowmem), and touches no persistent range.
2. Exactly one zImage and one FDT, and at most one initramfs.
3. The footprint belongs to this zImage: zreladdr = (load address & 0xf8000000) + 0x8000. The zImage must be in the first 128 MiB of RAM, or the new kernel takes its own 128 MiB-aligned base as PHYS_OFFSET and loses all the RAM below it.
4. The zImage sits above the kernel's _end, so head.S never relocates itself and the decompression never writes over it.
5. The FDT is clear of the kernel and the running zImage, and fits the two 1 MiB sections head.S maps at its address.
6. The initramfs is clear of the kernel, the zImage and the FDT; 7. no two segments overlap.
8. In the FDT handed over, every memreserve must be a persistent range, the FDT itself (rounded up to a page) or the initramfs. A stock FDT or initrd entry makes the new kernel reserve memory that is already dead, and says the FDT was passed on unrewritten. (`arm_memblock_init` handles the initrd before it scans the memreserves, so such an entry does not make the initramfs be dropped; an earlier note here said it did, which had the order wrong.)

The layout `koa2_plan_propose` gives for headless-smoke: the zImage at
0x80d00000, the FDT at 0x81400000, the initramfs at 0x81500000. It passes with
the built DTB, which has no memreserve. With the device's runtime DTB it is
refused by rule 8, on the two stock memreserves, and that is the expected
answer: the handoff has to pass an FDT that has been rewritten, which is the
next section.

## The handoff FDT rewrite (fdt-rewrite.c)

koa2_fdt_rewrite copies the stock runtime FDT token by token into a new one,
changing three things:
- the header reserve map becomes the list the caller gives (empty, for the smoke run);
- /chosen's bootargs, linux,initrd-start and linux,initrd-end are replaced (with no initramfs, both initrd properties are dropped); the new properties are written at the start of /chosen, before any subnode, and a /chosen is added as the root's last child if there is none;
- NOPs are dropped. Property names it needs and cannot find are appended to the strings block.

/memory is passed on as it stands: U-Boot writes 0x80000000 + 0x20000000, and
the measured hole at 0x9c000000 comes from reserved-memory/epdc_reserved's
no-map, a node that is also copied unchanged.

An empty reserve map is enough, because:
- the new kernel reserves the FDT itself in early_init_fdt_scan_reserved_mem;
- the initramfs is reserved from /chosen;
- the printk buffer (printk_reserve_buf), ramoops (imx7d_reserve) and the Falcon BIOS (falcon_mem_reserve) are reserved in code;
- the EPDC is reserved from reserved-memory.

A failure leaves nothing half-written (*bytes is 0 and what was written is
zeroed), and the output has to pass koa2_fdt_validate again. It allocates
nothing, so it goes into the kernel module as it is.

The test koa2-fdt-rewrite covers, under ASan and UBSan:
- replacing /chosen, and adding one when there is none;
- a second rewrite producing the same bytes;
- every output buffer too short to hold the result;
- edits out of range;
- 20000 mutated inputs.

check-plan.c runs the whole path over real files: propose a layout, rewrite the
FDT for it, validate the plan, and write out only the new FDT. On the device's
runtime DTB of 49280 bytes:
- the new FDT is 47972 bytes with no memreserve, and the plan validates;
- the layout is zImage 0x80d00000, FDT 0x81400000, initramfs 0x81500000–0x81542548;
- decompiled with dtc and compared against the original, only the two stock memreserves and the three /chosen properties differ.

Its output is kept in ~/koa2-evidence/handoff-plan/ and never in the
repository.

The FDT rewrite and layout validation also passed in device staging on 2026-11-21. This does not verify copying or jumping.

## The staging module (stage.c, koa2_handoff_stage.ko)

The module stages and checks; it has no jump. Experimental module faults can still hang or restart the stock kernel. No ioctl copies a payload to its
destination, stops a device, takes down the second core or leaves Linux, and
closing the device file frees every staged page with the stock system
untouched. The copy, the quiesce and the jump are the next stage, behind a
device and a gate of their own.

The path (/dev/koa2-handoff-stage, CAP_SYS_BOOT, one opener at a time):
1. At load it checks the stock kernel's identity: release `4.1.15-lab126`, version `#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023`, armv7l, and PHYS_OFFSET and PAGE_OFFSET both 0x80000000. Anything else and it refuses to load.
2. BEGIN: the caller declares the size and SHA256 of three slots (the zImage, the stock FDT as read from /sys/firmware/fdt, the initramfs). The zImage has to be the one the build pinned, or no page is allocated. Staged pages avoid the zImage load window 0x80000000–0x88000000 and the persistent ranges; a page that is avoided is held rather than freed, so the allocator cannot offer it again.
3. PUT: written in order, at most 64 KiB at a time. A failed user copy discards everything staged.
4. SEAL: the hashes, the pinned kernel, and the stock FDT (a Zelda, with every memreserve inside the measured exclusions). Then it proposes the layout, rewrites the FDT, checks /memory, and validates the plan. Last, it confirms that no staged page lies on a destination, because the copy stage reads the sources while it writes there. Any failure discards everything staged and records which check it was in STATUS's seal_stage and seal_result.
5. Finally it builds the copy list the jump stage would walk, and audits it (copylist.c). Nothing here walks it: this is the last thing checkable without leaving Linux, so it is checked now, on the device, rather than assumed later.
6. STATUS reports data only, never a physical address, and execute_available is always 0.

Building it: `ports/koa2/build-handoff-module.sh KERNEL_SRC KERNEL_OUT
PAYLOAD_OUT`. It generates target-lock.h from PAYLOAD_OUT, pinning the zImage's
size, hash and footprint and the bootargs, and before exporting it refuses
three things: a module that needs a GOT, one that imports a symbol the tree it
was built against does not export, and one that writes a coprocessor register
(mcr/mcrr) or imports cpu_down, a restart, __dump_lastk_to_mmc or
kallsyms_lookup_name. koa2-stage-client is the userspace side (a static ARM
binary) and does BEGIN/PUT/SEAL/STATUS and then closes.

The test koa2-stage compiles stage.c itself on the host, with the kernel API
replaced by boot/koa2/tests/kshim, under ASan and UBSan. It covers:
- the identity gate and the open rules;
- every refusal in BEGIN and PUT, including the allocator running dry and a faulting user buffer;
- every stage SEAL can fail at;
- pages inside the load window being avoided, and the count returning to zero after close.
Given the device's runtime DTB, 49280 bytes are rewritten to 47972 and the plan
validates.

## The copy list (copylist.c)

With the MMU off, the jump stage has no allocator, no page tables and no plan;
it has a list. Its shape is kexec's and PW3's: pages of 32-bit tagged entries,
all of them physical addresses.

| Tag | Meaning |
|---|---|
| KOA2_COPY_DEST | A page-aligned destination; the sources that follow go there in order |
| KOA2_COPY_SOURCE | Copy this page to the current destination |
| KOA2_COPY_LINK | The physical address of the next list page |
| KOA2_COPY_DONE | The end of the list |

koa2_copylist_audit is a deliberate second reader: it checks the written words
one by one, follows only a link it has verified, and refuses
- any source or list page lying inside a destination (which would let the copy order decide the result, and would let the walker overwrite the list it is reading);
- any source or list page inside a range that must outlive the jump;
- an entry that disagrees with the plan, a link to a page outside this list, anything after the terminator, a list page the walker never reaches;
- the same page twice in one segment, and an address unaligned or zero.

The test koa2-copylist does not only read the bytes back: it walks the list the
way the jump stage will, over an array standing in for physical memory, and
checks that every payload byte arrived where it belongs. Links, bounds,
overlaps and damaged entries all have cases. It found a miscount in the writer,
which compared words written, links included, against entries, which do not.

## The stock kernel's version checks (module-crcs.py)

The stock kernel has CONFIG_MODVERSIONS, no module signatures and no forced
loading. The Linux 4.1 loader (kernel/module.c) looks at two things:
- the vermagic has to match from the first space onwards (with CRCs present, same_magic skips the release), so `4.1.15-lab126-koa2-headless-smoke` against `4.1.15-lab126` is not by itself a refusal;
- module_layout and every imported symbol must carry the CRC the kernel exports, and an import with no entry in the module's __versions is refused too.

The stock kernel's own CRCs cannot be read on the device: they are absolute
symbols, which /proc/kallsyms leaves out. But every stock module records, in
its own __versions, the CRC of each symbol it was linked against.
ports/koa2/module-crcs.py takes the union of those and compares it entry by
entry with the handoff module's. It refuses outright if the stock modules
disagree with each other, if they carry more than one vermagic, if none is
found, if a __versions is the wrong length, or if a file is not ARM.
`--symvers` additionally reports whether the build tree and stock agree on
every symbol both know, which says whether rebuilding against that tree can
work at all. The test koa2-module-crcs covers these rules with modules
assembled byte by byte.

So that every import can be checked, stage.c no longer uses the four symbols no
stock module uses: __vmalloc became vzalloc, the ulong parameter became uint,
and page_to_phys with PHYS_OFFSET (which imports __pv_phys_pfn_offset under
ARM_PATCH_PHYS_VIRT) became virt_to_phys(page_address()), which is the same
thing on a KOA2 where all RAM is lowmem. Checked against the headless-smoke
build's own 97 modules standing in for stock: all 31 imports match, and the
build tree agrees with those modules on all 1037 symbols both know. That says
the tool and the module are consistent with each other, not that the stock
kernel will accept it.

Boot Preflight v2 (ports/koa2/preflight) copies the stock `.ko` files from
/lib/modules/$(uname -r) off the device, read-only and never loaded. Once they
are in hand, `KOA2_STOCK_MODULES=<directory>
ports/koa2/build-handoff-module.sh ...` runs this check before exporting and
refuses if anything disagrees.

Device evidence on 2026-11-21: this staging module loaded, sealed and unloaded successfully. 63 stock modules were recovered; offline CRC checking has not been run. This proves loader acceptance of this module, not compatibility of every module or kernel configuration. The report is linked from [current status](../../ports/koa2/STATUS.en.md).

## The jump trampoline (jump.S)

Turns off the MMU, walks the copy list, enters the new kernel. **It is built
into no module**, and the module has no way to reach it. It has been reviewed
on the host and run bare-metal on QEMU.

KOA2 is simpler than PW3 in one place: PAGE_OFFSET and PHYS_OFFSET are both
0x80000000 and lowmem is already identity mapped, so when the trampoline is
entered at the physical address of its own page, that address is also its
virtual one, and clearing the M bit does not move the instruction stream. So
**no private page table is needed**, which PW3 does need, being 3G/1G. The
runtime DTB also has no outer L2 controller (the A7's L2 is in the core), and
CONFIG_ARM_KERNMEM_PERMS is off, so lowmem pages are executable.

What the caller must have done, none of which the trampoline checks for it:
- one core left (the second taken down with the stock cpu_down);
- SVC mode, ARM state, IRQ and FIQ masked;
- every DMA master quiesced (handoff-quiesce.md);
- the caches cleaned to RAM and disabled, TTBCR 0;
- the trampoline copied into a page the handoff owns, and entered at that page's physical address;
- a copy list koa2_copylist_audit has accepted.

The trampoline itself has no stack, no external call, no literal pool, and
writes nothing but its own parameter block. objdump confirms it has no
relocations, no `ldr rX, [pc, ...]`, never touches sp and has no bl or blx, so
it can run from the page it was copied into. KJ_BUDGET takes the entry count
the audit produced: a list the audit accepted cannot reach that ceiling, and a
damaged one stops instead of spinning.

A refusal records a status and halts. Once the caches are off there is no way
back, so there is no path that returns to the caller. Refusals come in two
kinds: those findable before the first entry is read (the caches still on, a
link or source out of bounds), where nothing has been copied; and those only
findable part way through (the budget exhausted, the wrong page count at the
end), where the pages already copied stay copied. jump-abi.h and the test both
record which is which.

### The bare-metal test (tests/baremetal, koa2-jump-baremetal)

What the host tests cannot reach is exactly this: carrying on from the same
physical address after the M bit is cleared, finding the parameter block
through the PC, and the register contract at the jump. So the real jump.S and
the real copy-list code run bare-metal on QEMU's Cortex-A7:

- `pass`: the list is walked and the stand-in kernel is entered and checks for itself that r0 is 0, r1 is the machine number and r2 points at the FDT it was given (it prints KO! only if all three hold);
- `caches_on`, `bad_link`, `bad_source`, `short_count`, `budget`: five refusals, where both the status and whether any page was copied are read out of the trampoline's own parameter block through the QEMU monitor.

Without the cross compiler or QEMU the test skips itself. It says this code
behaves as designed on an ARMv7 core; it says nothing about how the i.MX7D's
peripherals behave on real hardware.

## The quiesce sequence (quiesce.c)

The order in section four of handoff-quiesce.md, written as a table where each
step is a run and, before the point of no return, an undo. What each step does
to the hardware needs the device; what can be checked here is the thing nobody
would try on the device: **if step 6 fails, is the stock system as it was?**

Ten steps, in order: validate, EPDC/ePXP, Wi-Fi (usdhc1), the USB gadget, SDMA,
eMMC idle, the second core with cpu_down(1), mask IRQ and FIQ, feed the
watchdog once, caches off. The last is the **point of no return**: with the
caches off there is no stock kernel to go back to, and only the new kernel or
the watchdog remain.

koa2_quiesce_check refuses these two tables before a single step runs:
- a step before the point of no return with no undo (it would change the stock system with no way back);
- a step at or after it that has one (claiming a way back that does not exist).

When a step fails, the completed steps are undone in reverse and that step's
own error is returned. An undo that itself fails is recorded while the rest are
still attempted: one thing that cannot be given back must not strand the
others.

The test koa2-quiesce tries a failure at **every** step and checks the order
and count of the rollback, the record of a failing undo, and that both bad
tables above are refused. None of it touches hardware.

### Who stops the devices (the design changed on 2026-11-21)

The plan had been for the module to stop them. Neither half of that works.
**Almost nothing that stops a device can be checked**: the build tree does not
export `clk_disable_unprepare`, `dmaengine_terminate_all` or
`mmc_suspend_host`, and `lock_fb_info`'s CRC differs from stock, so a module
that touched the framebuffer would be refused at load. And **a stopped
device's registers cannot be read**: once its clock is gated, reading a
peripheral register on i.MX7 hangs the bus rather than returning a value, so a
check written that way would hang the device in exactly the case where
quiescing had worked.

So the stopping is done from userspace
(`ports/koa2/stage-test/quiesce-dryrun.sh`) through the kernel's own driver
removal and `/sys/.../cpu1/online` paths, which are written for it and are
reversible. That also settled an older open item: the EPDC's remove path frees
its DMA framebuffers, so there is no longer anything to read at handoff time
and exclude. The eMMC is the exception, being the root filesystem: sync and a
read-only remount is as far as it goes.

The module only checks the result (`quiesce-verify.c`): one core online, none
of the four drivers loaded, the rootfs read-only, and the watchdog fed within
ten seconds, **reading no peripheral register**. That evidence is weaker than
reading an engine's status register, which the comments say plainly, along with
why "the engines are passive, so stopping their clients stops the traffic" is
an argument and not a measurement. The test `koa2-quiesce-verify` watches one
thing: that a machine which is not quiesced is never accepted, including the
failure where the check passed because it did not look.

`koa2-quiesce-dryrun` runs the real script against a simulated device and
checks the order, that every device is put back, that the rootfs goes
read-write before any reload, and the four refusals. See section six of
[ports/koa2/handoff-quiesce.md](../../ports/koa2/handoff-quiesce.md).
