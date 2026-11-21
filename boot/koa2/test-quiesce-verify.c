/* SPDX-License-Identifier: GPL-2.0-only */
/* What the module accepts as a quiesced machine.
 *
 * The checks are weak on purpose -- no peripheral register can be read once
 * its clock is gated -- so the one thing that must not happen is accepting a
 * machine that is not quiesced. Every case below is a way for that to be true
 * while looking fine. */
#include "quiesce-verify.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct koa2_quiesce_state good(void)
{
    struct koa2_quiesce_state s;
    memset(&s, 0, sizeof(s));
    s.online_cpus = 1;
    s.expected_cpus = 1;
    s.drivers_present = 0;
    s.drivers_checked = KOA2_Q_DRIVER_COUNT;
    s.rootfs_readonly = 1;
    s.seconds_since_feed = 0;
    return s;
}
int main(void)
{
    struct koa2_quiesce_state s;
    unsigned i;
    s = good();
    assert(koa2_quiesce_verify(&s) == KOA2_OK);
    /* The second core still up, or back up. */
    s = good(); s.online_cpus = 2; assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    s = good(); s.online_cpus = 0; assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    /* A machine with more cores than this one: the layout was measured on a
     * two-core device and nothing here knows what a third would be doing. */
    s = good(); s.expected_cpus = 2; assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    /* Any driver still loaded. */
    for (i = 1; i <= KOA2_Q_DRIVER_COUNT; i++) {
        s = good(); s.drivers_present = i;
        assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    }
    /* Looked for fewer drivers than there are: a check that passed because it
     * did not look is the one failure this cannot afford. */
    for (i = 0; i < KOA2_Q_DRIVER_COUNT; i++) {
        s = good(); s.drivers_checked = i;
        assert(koa2_quiesce_verify(&s) == KOA2_BOUNDS);
    }
    s = good(); s.drivers_checked = KOA2_Q_DRIVER_COUNT + 1;
    assert(koa2_quiesce_verify(&s) == KOA2_BOUNDS);
    /* The rootfs still writable: eMMC can still be written under us. */
    s = good(); s.rootfs_readonly = 0; assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    /* The watchdog fed too long ago. It cannot be stopped, so an unknown
     * margin is a refusal. */
    s = good(); s.seconds_since_feed = KOA2_Q_FEED_WINDOW_SECONDS;
    assert(koa2_quiesce_verify(&s) == KOA2_OK);
    s = good(); s.seconds_since_feed = KOA2_Q_FEED_WINDOW_SECONDS + 1;
    assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    s = good(); s.seconds_since_feed = 60; assert(koa2_quiesce_verify(&s) == KOA2_RANGE);
    assert(koa2_quiesce_verify(NULL) == KOA2_BOUNDS);
    /* The driver list is the four removable masters, and eMMC is not among
     * them: it is the root filesystem and cannot be stopped from userspace. */
    assert(KOA2_Q_DRIVER_COUNT == 4);
    for (i = 0; i < KOA2_Q_DRIVER_COUNT; i++) {
        assert(koa2_quiesce_drivers[i] && koa2_quiesce_drivers[i][0]);
        assert(strcmp(koa2_quiesce_drivers[i], "falcon"));
        assert(!strstr(koa2_quiesce_drivers[i], "mmc"));
    }
    assert(!strcmp(koa2_quiesce_drivers[0], "mxc_epdc_v2_fb"));
    puts("KOA2 quiesce verify: one core, every listed driver gone, a read-only rootfs and a recent "
         "watchdog feed; a check that did not look is refused");
    return 0;
}
