/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_COPYLIST_H
#define KOA2_COPYLIST_H
#include "plan.h"
/* The list the jump stage walks with the MMU off to put each staged page at its
 * destination. Same shape as kexec's and PW3's: a chain of pages of 32-bit
 * tagged entries, so the walker needs no allocator, no page tables and no
 * knowledge of the plan.
 *
 *   dest   | KOA2_COPY_DEST    page-aligned destination; following sources go
 *                              here, one page each, ascending
 *   source | KOA2_COPY_SOURCE  copy this page to the current destination
 *   next   | KOA2_COPY_LINK    physical address of the next list page
 *   KOA2_COPY_DONE             end of the list
 *
 * Every address in it is physical. Because no source or list page may lie in a
 * destination (koa2_copylist_audit refuses that), the order of the copies does
 * not matter and the walker cannot overwrite the list it is reading. */
#define KOA2_COPY_DEST 1u
#define KOA2_COPY_LINK 2u
#define KOA2_COPY_DONE 4u
#define KOA2_COPY_SOURCE 8u
#define KOA2_COPY_TAGS 0xfu
#define KOA2_COPY_WORDS (0x1000u / 4u)   /* entries per page; the last may hold a link */
/* One segment: where it goes, and the staged pages holding it in order. */
struct koa2_copy_segment { uint64_t dest; const uint32_t *pages; size_t page_count; };
/* How many list pages a set of segments needs. 0 if it cannot be represented. */
size_t koa2_copylist_pages(const struct koa2_copy_segment *segments, size_t count);
/* Writes the list into list_virtual[0..list_count), whose physical addresses are
 * list_physical[]. *entries is the words written, links included. Does not
 * validate placement; call the audit next. */
int koa2_copylist_build(uint32_t *const *list_virtual, const uint32_t *list_physical, size_t list_count,
                        const struct koa2_copy_segment *segments, size_t segment_count, size_t *entries);
/* Reads the written list back, following only links it has verified, and checks
 * it against the segments: every entry in order, every link a page of this
 * list, the tail of the last page zero, and no source or list page inside a
 * destination or a forbidden range. KOA2_OK means the walker can be trusted
 * with it. */
int koa2_copylist_audit(const uint32_t *const *list_virtual, const uint32_t *list_physical, size_t list_count,
                        const struct koa2_copy_segment *segments, size_t segment_count,
                        const struct koa2_range *forbidden, size_t forbidden_count,
                        size_t *entries, size_t *pages);
#endif
