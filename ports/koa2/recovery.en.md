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

The fix is cheap and is not implemented yet: **have the handoff module write a
marker into the printk buffer before jumping** (say `KOA2_HANDOFF` with a
counter and a timestamp). Then, after the reset:

| What `/proc/printkbuf` shows | How far it got |
|---|---|
| No marker | The jump never happened, or the printk buffer was not preserved, which would mean section two is wrong |
| The marker alone | It jumped, but the new kernel died before printk came up |
| The marker followed by a new kernel's log | The new kernel ran and died later, and the log says where |

Formatting that marker is implemented (`boot/koa2/marker.c`, test
`koa2-marker`), using only capabilities whose CRCs are already checked
(`__arm_ioremap` is on the usable list), so it adds no new symbol risk. It is
**not wired up yet**: writing it into the printk buffer belongs to the jump
stage, and the jump stage is still in no module.

## 5. So these all have to hold before a jump is discussed

| Condition | Where it stands |
|---|---|
| No partition written, no bootloader changed | Holds, and always has |
| The watchdog ends a failed attempt within 60 seconds | Holds in source (WDE cannot be cleared) |
| The reset is a Partial Restart and DRAM survives | Holds by reasoning from source, **unverified on the device** |
| The printk buffer and ramoops are not overwritten by the handoff | Holds (in the persistent exclusions, with a test on it) |
| A marker written before the jump, so an early failure is distinguishable | Formatting is implemented and tested; **the write is not wired up** (it belongs to the jump stage) |
| Quiescing works and everything can be put back | The script is written, **never run on the device** (the dry run) |

Those two "not"s are what to do next; a jump on the device should not be
scheduled until they are done.
