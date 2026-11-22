/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_MARKER_H
#define KOA2_MARKER_H
#include "contract.h"
/* A line written into the Lab126 printk buffer just before the jump, so that a
 * failure which never reaches the new kernel's printk can be told apart from a
 * jump that never happened.
 *
 * Why it is needed: the stock kernel copies the previous contents of
 * 0x9be00000 to the second half of that area on every boot, and that copy is
 * what /proc/printkbuf reads. So the new kernel's dying words survive a
 * watchdog reset -- but only if it got as far as writing any. Dying in the
 * decompressor or in head.S leaves the stock kernel's own old log there, which
 * reads exactly like nothing having happened. See ports/koa2/recovery.md.
 *
 * The marker is plain text with no pointers and no lengths to get wrong: it is
 * read by a person, after a reset, out of a buffer that may be damaged. */
#define KOA2_MARKER_MAGIC "KOA2_HANDOFF"
#define KOA2_MARKER_MAX 120
/* Writes "KOA2_HANDOFF v1 attempt=N uptime=Ts entry=0x... fdt=0x...\n" into out,
 * returning its length, or 0 if it does not fit. Pure: it formats, and the
 * caller decides where that goes. */
size_t koa2_marker_format(char *out, size_t max, unsigned attempt,
                          unsigned long uptime_seconds, uint32_t entry, uint32_t fdt);

/* Putting one into the printk buffer.
 *
 * It goes in the LAST KOA2_MARKER_SLOT bytes of the buffer's live half, which
 * is the half the next boot saves as the previous boot's log. The slot is
 * cleared first, so where the marker ends is unambiguous to whoever reads it.
 * That costs up to KOA2_MARKER_SLOT bytes of this boot's own log, at the end of
 * a boot that is about to stop having one.
 *
 * Nothing here locks against the kernel's own printk, which writes into the
 * same half; logbuf_lock is private to printk.c and a module cannot take it. A
 * printk landing in the slot damages the marker, so the magic leads -- enough
 * survives to recognise -- and placing it again with a higher attempt is cheap.
 *
 * write_back is a parameter, and without it this refuses, because a watchdog
 * reset on this SoC does not clean the caches: a marker still in a dirty cache
 * line is a marker that was never written. See ports/koa2/recovery.md. */
#define KOA2_MARKER_SLOT 256u
struct koa2_marker_target {
    char *buffer;       /* the live half of the printk buffer, as the kernel maps it */
    size_t bytes;       /* how many bytes that half is */
    uint32_t phys;      /* its physical address, which the write-back needs */
};
/* Returns the marker's length, or 0 having written nothing. */
size_t koa2_marker_place(const struct koa2_marker_target *target, unsigned attempt,
                         unsigned long uptime_seconds, uint32_t entry, uint32_t fdt,
                         void (*write_back)(void *context, char *va, uint32_t phys, size_t bytes),
                         void *context);
#endif
