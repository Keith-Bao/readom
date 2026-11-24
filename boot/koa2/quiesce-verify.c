/* SPDX-License-Identifier: GPL-2.0-only */
#include "quiesce-verify.h"
const char *const koa2_quiesce_drivers[KOA2_Q_DRIVER_COUNT] = {
    "mxc_epdc_v2_fb",   /* EPDC and, through it, ePXP */
    "bcmdhd",           /* Wi-Fi on usdhc1 */
    "g_mass_storage",   /* the USB gadget */
    "cyttsp5_i2c",      /* touch, which takes interrupts */
};
int koa2_quiesce_verify(const struct koa2_quiesce_state *s)
{
    if (!s) return KOA2_BOUNDS;
    /* One core. More means cpu1 did not go down, or came back. */
    if (s->expected_cpus != 1 || s->online_cpus != 1) return KOA2_RANGE;
    /* Every driver that commands a master is gone, and the module looked for
     * all of them rather than some. */
    if (s->drivers_checked != KOA2_Q_DRIVER_COUNT) return KOA2_BOUNDS;
    if (s->drivers_present) {
        int touch_only = (s->drivers_present == 1 &&
                          s->touch_driver_present == 1 &&
                          s->touch_quiesced_verified == 1);
        if (!touch_only) return KOA2_RANGE;
    }
    if (!s->rootfs_readonly) return KOA2_RANGE;
    /* The watchdog is at 60 seconds and cannot be stopped. Everything after
     * this point -- the copy, the jump, and the new kernel reaching its own
     * watchdog driver -- has to fit in what is left, so the feed has to be
     * recent or the margin is unknown. */
    if (s->seconds_since_feed > KOA2_Q_FEED_WINDOW_SECONDS) return KOA2_RANGE;
    return KOA2_OK;
}
