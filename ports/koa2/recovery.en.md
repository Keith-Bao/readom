# Coming back from a failed handoff, and knowing why it failed (from source, unverified on the device)

[ROADMAP.md](ROADMAP.en.md) sets a hard gate: **work on the device stops if the
handoff cannot be shown to be recoverable**. This is what that proof should
look like, and what it is still missing.
Chinese: [recovery.md](recovery.md).

The conclusion first: **it does come back, and it will probably say why**, but
one failure mode is silent today, and section four gives the cheap fix. No jump
has ever been performed; all of this is reasoning from source.

## 1. What happens after the jump

After the jump the stock kernel no longer exists: the moment the caches go off
there is nothing left to return to. So recovery does not mean returning to the
stock kernel. It means **the device boots the stock system from eMMC again**.

| Case | What ends it | How long |
|---|---|---|
| The new kernel comes up | Nothing needs to end it | — |
| It does not come up, or hangs after it does | The i.MX watchdog, at 60 seconds | 60 seconds at most |
| It dies before reaching the watchdog at all | The same; the watchdog was already counting | 60 seconds at most |
| The user does not want to wait | Hold power about 40 seconds | At once |

The watchdog's WDE bit cannot be cleared once set (section two of
[handoff-quiesce.md](handoff-quiesce.md)), so **it keeps counting across the
handoff**, which is the backstop: wherever the new kernel dies, the device
resets within 60 seconds and U-Boot boots the stock kernel from eMMC. No
partition is written and no bootloader changed, so the stock system itself is
intact.

## 2. Why the log survives the reset

This step is what decides whether a failure can be analysed. The stock
`imx2_wdt` has `fsl,wdog_b`, so a timeout drives the WDOG_B pin into the
MAX77796's reset input. The PMIC has two classes of restart
(`drivers/mfd/max77696-topsys.c`):

- **Full Restart** (`FRSTRT`), which restarts the rails completely;
- **Partial Restart** (`PRSTRT`), which keeps some rails up.

In the reset-reason table, `RSTIN_PRSTRT` (Reset Input Partial Restart) carries
the string `reset=watchdog` — so **a watchdog reset through WDOG_B is a Partial
Restart**, not a power-cycling Full Restart. Lab126 keeping a printk buffer in
DRAM across boots at all only makes sense if that class of reset preserves it.

## 3. Where to read the log afterwards

The stock kernel does this on every boot (`kernel/printk/printk.c`, under
`CONFIG_LAB126_PRINTK_BUFFER`):

1. `log_goal_first` is 0x9be00000, where this boot writes its printk;
2. the **previous** contents of that area are copied to 0x9be40000, the second 256 KiB;
3. the first half is then cleared, so an old log does not keep reappearing.

`/proc/printkbuf` reads the copy made in step 2 — that is, **the previous
boot's log**. So the path after a failure is:

```
the new kernel writes its log to 0x9be00000 -> it dies -> the watchdog resets after 60 s
-> the stock kernel boots and copies 0x9be00000 to the backup half
-> dd if=/proc/printkbuf reads the new kernel's last words
```

Both halves (0x9be00000 + 0x80000) are in the handoff's persistent exclusions
(`koa2_handoff_persistent`), so the copy never touches them, and the new kernel
has `CONFIG_LAB126_PRINTK_BUFFER` itself, so it reserves the same area rather
than using it as ordinary memory. ramoops (0x9bf00000 + 0x100000) is excluded
the same way, and the new kernel's `CONFIG_MX6_RAMOOPS=y` allocates it at the
same place, so a panic also leaves a copy in `/sys/fs/pstore`.

## 4. One failure is silent (the real gap today)

If the new kernel dies **before printk is initialised** — still decompressing,
still in `head.S`, or unable to read the FDT — then 0x9be00000 still holds the
**stock kernel's previous log**. After the reset the stock kernel reads
`/proc/printkbuf` and sees its own old log, which is indistinguishable from
"nothing happened".

This is not a hypothetical worry: decompression and early boot are the most
likely places to fail, and they fall exactly in this blind spot.

The fix is cheap and **is now wired up**: the handoff module's `K2S_MARK`
writes a marker into the printk buffer
(`KOA2_HANDOFF v1 attempt=N uptime=Ts entry=0x... fdt=0x...`). After the reset:

| What `/proc/printkbuf` shows | How far it got |
|---|---|
| No marker | The jump never happened, or the printk buffer was not preserved, which would mean section two is wrong |
| The marker alone | It jumped, but the new kernel died before printk came up |
| The marker followed by a new kernel's log | The new kernel ran and died later, and the log says where |

Writing it is implemented (`koa2_marker_place`, `K2S_MARK` in the module, tests
`koa2-marker`, `koa2-stage`, `koa2-marker-wiring`). It is **not** a jump and
does not open the way to one: no payload is copied, no device is stopped, no
core goes down, and `execute_available` stays 0. What it costs is the last 256
bytes of one boot's kernel log.

Three things about the wiring differ from what was written here before, and all
three only showed up when the module was actually compiled and its CRCs checked:

- **It goes at the end of the live half**, not the start. The live half is what
  the next boot saves, and the start is exactly where printk itself begins
  writing. The 256 bytes are cleared first, so a reader can tell where the
  marker ends.
- **It has to clean itself out of the caches.** A watchdog reset on this SoC
  does not flush them — that is what PW3's
  `1043-printk-ring-survives-watchdog-reset.patch` exists for — so a marker left
  in a dirty line disappears in the very reset it was meant to explain.
  `koa2_marker_place` refuses without a write-back: written but unable to get
  out looks like success and is not.
- **`__arm_ioremap`, named here before, cannot be used.** On RAM it does
  `WARN_ON(pfn_valid(pfn))` and returns NULL, and this is RAM the kernel
  reserved and mapped, so `pfn_valid` holds. `phys_to_virt` is used instead,
  which imports nothing.

The choice of symbols is not free either, and two of them were traps. Cleaning
by address (`__cpuc_flush_dcache_area`) compiles to
`v7_flush_kern_dcache_area`, which no stock module imports: its CRC cannot be
checked, so the stock loader refuses the whole module. `get_jiffies_64()/HZ`
pulls in `__aeabi_uldivmod`, which stock does not export at all and the build
script refuses outright. What is used now is `flush_cache_all` (which compiles
to `v7_flush_kern_cache_all`, imported by `mxc_epdc_v2_fb`) and the 32-bit
`jiffies`. All 35 of the module's imports match stock CRCs, `passed: true`.
`koa2-marker-wiring` keeps both traps from coming back.

The one thing a marker has to prove on the device — that it survives a reset —
can only be done in two halves: place one, restart, then read
`/proc/printkbuf`, which publishes the previous boot's buffer. In KUAL those are
"Marker test: place one" and "Marker test: check after restart"
(`ports/koa2/stage-test/marker-test.sh`). Neither has been run on the device.

## 5. So these all have to hold before a jump is discussed

| Condition | Where it stands |
|---|---|
| No partition written, no bootloader changed | Holds, and always has |
| The watchdog ends a failed attempt within 60 seconds | Holds in source (WDE cannot be cleared) |
| The reset is a Partial Restart and DRAM survives | Holds by reasoning from source, **unverified on the device** |
| The printk buffer and ramoops are not overwritten by the handoff | Holds (in the persistent exclusions, with a test on it) |
| A marker written before the jump, so an early failure is distinguishable | The write is wired up (`K2S_MARK`), host tests pass and every stock CRC matches; **no marker has been placed on the device, and none has been shown to survive a reset** |
| Quiescing works and everything can be put back | The script is written, **never run on the device** (the dry run) |

What is left of those "not"s has to happen on the device, and neither part
touches the jump: place a marker, restart, confirm it is still there
(`marker-test.sh`), and the quiesce dry run (`quiesce-dryrun.sh`). A jump on the
device should not be scheduled until they are done.

That marker run also tests what section two only reasons about from source: that
the reset is a Partial Restart and DRAM survives. If the marker does not
survive, section two is wrong, and that matters more than the marker does.
