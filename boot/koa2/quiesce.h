/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_QUIESCE_H
#define KOA2_QUIESCE_H
#include "contract.h"
/* The order the handoff stops the machine in, as a sequence this file can
 * reason about without touching hardware.
 *
 * The property worth testing is not what each step does to a register -- that
 * needs the device -- but that the sequence has a point of no return and knows
 * where it is. Every step before it must be undoable, and if any of them
 * fails the stock system has to be put back exactly as it was, in reverse. At
 * the point of no return the caches go off; after that there is no stock
 * kernel left to return to, so there is nothing to undo and the only paths are
 * the new kernel or the watchdog.
 *
 * ports/koa2/handoff-quiesce.md is where each step's evidence lives.
 *
 * Who does what, and why it is split that way (ports/koa2/stock-module-abi.md):
 * almost nothing that STOPS a device is both exported to a module and has a
 * stock CRC to check it against, and lock_fb_info's CRC actively differs, so a
 * module that touched the framebuffer would be refused at load. Worse, a
 * peripheral's registers cannot be read once its clock is gated -- which is
 * exactly the state a stopped device is in -- so a module that verified by
 * reading registers would hang the bus precisely when quiescing had worked.
 *
 * So the stopping is done from userspace, through the kernel's own driver
 * removal and CPU hotplug paths, which are written for it and are reversible;
 * the module only checks the result, using symbols whose CRCs the device's own
 * modules confirm. Each step below says which side it belongs to. */
enum koa2_quiesce_step {
    /* Userspace, before the module is asked to do anything. Each is undone by
     * loading the module again or putting the core back online. */
    KOA2_Q_VALIDATE = 0,   /* re-check the plan and the list; changes nothing */
    KOA2_Q_EPDC,           /* rmmod mxc_epdc_v2_fb: its remove path waits for the
                            * updates in flight, powers the panel down and frees
                            * the DMA framebuffers, so there is nothing left to
                            * exclude from the layout */
    KOA2_Q_WIFI,           /* rmmod bcmdhd: stops the SDIO function on usdhc1 */
    KOA2_Q_USB,            /* rmmod g_mass_storage and friends: disconnects the
                            * gadget and stops the controller */
    KOA2_Q_INPUT,          /* rmmod the touch and sensor drivers (I2C, no DMA of
                            * their own, but they take interrupts) */
    KOA2_Q_STORAGE,        /* sync and remount the rootfs read-only. eMMC cannot
                            * be stopped while userspace runs from it; this is as
                            * far as it goes, and the window after it is short */
    KOA2_Q_SECOND_CORE,    /* /sys/devices/system/cpu/cpu1/online = 0, which is
                            * the stock cpu_down path without importing it */
    /* The module, which from here on is the only thing running that matters. */
    KOA2_Q_VERIFY,         /* check what userspace claims: the core is down and
                            * the named drivers are gone. Reads no peripheral
                            * register, because their clocks are now off */
    KOA2_Q_IRQ,            /* mask IRQ and FIQ on the one core left */
    KOA2_Q_WATCHDOG,       /* feed it once, so the new kernel gets a full timeout */
    KOA2_Q_CACHES,         /* the point of no return: clean and disable */
    KOA2_Q_COUNT
};
/* The first step the module performs; everything below it is userspace's. */
#define KOA2_Q_FIRST_IN_MODULE KOA2_Q_VERIFY
/* The first step that cannot be undone. Steps below it must restore on abort. */
#define KOA2_Q_IRREVERSIBLE KOA2_Q_CACHES
struct koa2_quiesce_ops {
    /* Each returns 0 or a negative error. undo is NULL only for steps at or
     * after KOA2_Q_IRREVERSIBLE. */
    int (*run)(void *context);
    int (*undo)(void *context);
    const char *name;
};
struct koa2_quiesce {
    struct koa2_quiesce_ops step[KOA2_Q_COUNT];
    void *context;
    /* Filled in as it goes, so a failure says exactly how far it got. */
    unsigned done;              /* steps completed */
    int failed_step, failed_result;
    unsigned undone;            /* steps rolled back */
    int undo_failed_step, undo_failed_result;
};
/* Checks the table before anything runs: every step present, and undo present
 * exactly for the reversible ones. */
int koa2_quiesce_check(const struct koa2_quiesce *q);
/* Runs the steps in order. On success, the machine is at the point of no
 * return and KOA2_OK is returned. On failure it undoes the steps it completed,
 * in reverse, and returns the step's own error; the stock system is back.
 * An undo that itself fails is recorded and the rest are still attempted. */
int koa2_quiesce_run(struct koa2_quiesce *q);
#endif
