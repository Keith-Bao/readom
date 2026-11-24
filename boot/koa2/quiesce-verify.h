/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_QUIESCE_VERIFY_H
#define KOA2_QUIESCE_VERIFY_H
#include "contract.h"
/* What the module can establish about a quiesced machine, and nothing more.
 *
 * It deliberately reads no peripheral register. A device whose driver is gone
 * has its clock gated, and on i.MX7 a register read with the clock gated does
 * not return a value -- it hangs the bus. So a check written that way would
 * hang the device in exactly the case where quiescing had worked. What is left
 * is evidence that does not touch the hardware: which cores are online, and
 * which drivers are still loaded.
 *
 * That evidence is weaker than reading a DMA engine's status, and saying so is
 * the point: this says the drivers that command the engines are gone and their
 * removal paths ran, not that every engine is idle. The engines on this machine
 * are passive (SDMA and APBH move data only when a client asks, and their
 * clients are those drivers), which is the argument for why the one implies the
 * other -- an argument, not a measurement. ports/koa2/handoff-quiesce.md. */
struct koa2_quiesce_state {
    unsigned online_cpus;       /* how many cores are online now */
    unsigned expected_cpus;      /* how many there should be: 1 */
    unsigned drivers_present;    /* of the names below, how many are still loaded */
    unsigned drivers_checked;
    unsigned rootfs_readonly;    /* 1 when userspace says it remounted it */
    unsigned long seconds_since_feed;  /* since the watchdog was last fed */
    /* Optional reversible touch-quiesce path (proven on KOA2 2026-11-24/25):
     * cyttsp5_i2c may remain loaded ONLY when it is the sole driver present
     * AND userspace independently verified MAX77796-LSW1=disabled, zero I2C2
     * and touch interrupts over the quiet window, and irq_thread=S. */
    unsigned touch_driver_present;
    unsigned touch_quiesced_verified;
};
/* The drivers that command a DMA master on this machine and are removable.
 * usdhc3 (eMMC) is not here: it is the root filesystem and cannot be stopped
 * while userspace is running from it. */
#define KOA2_Q_DRIVER_COUNT 4
extern const char *const koa2_quiesce_drivers[KOA2_Q_DRIVER_COUNT];
/* KOA2_OK only when one core is online, no listed driver is loaded, the rootfs
 * is read-only, and the watchdog was fed recently enough that the new kernel
 * gets a usable share of the 60-second timeout. Pure: it decides, and reads
 * nothing itself. */
#define KOA2_Q_FEED_WINDOW_SECONDS 10
int koa2_quiesce_verify(const struct koa2_quiesce_state *state);
#endif
