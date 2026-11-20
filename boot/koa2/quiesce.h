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
 * ports/koa2/handoff-quiesce.md is where each step's evidence lives. */
enum koa2_quiesce_step {
    KOA2_Q_VALIDATE = 0,   /* re-check the plan and the list; changes nothing */
    KOA2_Q_EPDC,           /* wait for the current update, then stop EPDC/ePXP */
    KOA2_Q_WIFI,           /* stop the Wi-Fi SDIO function on usdhc1 */
    KOA2_Q_USB,            /* disconnect the gadget, stop the controller */
    KOA2_Q_SDMA,           /* stop every SDMA channel (serves UART, ECSPI, SAI) */
    KOA2_Q_STORAGE,        /* let eMMC go idle; no request may be in flight */
    KOA2_Q_SECOND_CORE,    /* the stock cpu_down(1); checked with cpu_online(1) */
    KOA2_Q_IRQ,            /* mask IRQ and FIQ on the one core left */
    KOA2_Q_WATCHDOG,       /* feed it once, so the new kernel gets a full timeout */
    KOA2_Q_CACHES,         /* the point of no return: clean and disable */
    KOA2_Q_COUNT
};
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
