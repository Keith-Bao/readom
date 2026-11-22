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
#endif
