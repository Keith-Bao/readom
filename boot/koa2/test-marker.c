/* SPDX-License-Identifier: GPL-2.0-only */
/* The marker is read by a person, out of a possibly damaged buffer, after the
 * device has reset. So it has to be plain, complete, and never run off the end
 * of the buffer it is given -- which is the printk buffer, and writing past it
 * would destroy the very log it exists to make readable. */
#include "marker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Placing one. The buffer here stands for the live half of the Lab126 printk
 * buffer: the half the next boot saves and /proc/printkbuf then publishes. */
#define HALF 4096u
static unsigned backs;
static char *back_va;
static uint32_t back_phys;
static size_t back_bytes;
static void record(void *context, char *va, uint32_t phys, size_t bytes)
{
    assert(context == (void *)0x1234);
    backs++; back_va = va; back_phys = phys; back_bytes = bytes;
}
static void place(void)
{
    static char half[HALF];
    struct koa2_marker_target t = { half, HALF, 0x9be00000u };
    const char *slot = half + HALF - KOA2_MARKER_SLOT;
    size_t n, i;
    memset(half, 'L', sizeof(half));            /* this boot's log */
    backs = 0;
    n = koa2_marker_place(&t, 3, 77, 0x80d00000u, 0x81400000u, record, (void *)0x1234);
    assert(n && n <= KOA2_MARKER_SLOT);
    /* It lands at the END of the half. That is the part the next boot saves,
     * and the start is where printk itself begins writing. */
    assert(!strncmp(slot, KOA2_MARKER_MAGIC, strlen(KOA2_MARKER_MAGIC)));
    assert(!strcmp(slot, "KOA2_HANDOFF v1 attempt=3 uptime=77s entry=0x80d00000 fdt=0x81400000\n"));
    /* The rest of the slot is cleared, so where the marker ends is unambiguous;
     * the log before the slot is left alone. */
    for (i = n; i < KOA2_MARKER_SLOT; i++) assert(slot[i] == 0);
    for (i = 0; i < HALF - KOA2_MARKER_SLOT; i++) assert(half[i] == 'L');
    /* Cleaned out to DRAM at the slot's own physical address: a marker left in
     * a dirty cache line does not survive the reset it exists for. */
    assert(backs == 1 && back_va == slot && back_bytes == KOA2_MARKER_SLOT);
    assert(back_phys == 0x9be00000u + HALF - KOA2_MARKER_SLOT);
    /* Again: the newer attempt replaces it, and it is cleaned again. */
    assert(koa2_marker_place(&t, 4, 78, 0x80d00000u, 0x81400000u, record, (void *)0x1234));
    assert(backs == 2 && strstr(slot, "attempt=4"));
    /* No write-back: refuse, and write nothing. Writing anyway would leave
     * something that reads like success and would not survive a reset. */
    memset(half, 'L', sizeof(half));
    assert(koa2_marker_place(&t, 1, 1, 1, 1, NULL, NULL) == 0);
    for (i = 0; i < HALF; i++) assert(half[i] == 'L');
    /* Refusals: no target, no buffer, a half too small to hold both a slot and
     * a log, and a physical address that would wrap. */
    assert(koa2_marker_place(NULL, 1, 1, 1, 1, record, (void *)0x1234) == 0);
    {   struct koa2_marker_target bad = { NULL, HALF, 0x9be00000u };
        assert(koa2_marker_place(&bad, 1, 1, 1, 1, record, (void *)0x1234) == 0); }
    {   struct koa2_marker_target small = { half, 2 * KOA2_MARKER_SLOT - 1, 0x9be00000u };
        assert(koa2_marker_place(&small, 1, 1, 1, 1, record, (void *)0x1234) == 0); }
    {   struct koa2_marker_target wrap = { half, HALF, 0xffffffffu - HALF + 1 };
        assert(koa2_marker_place(&wrap, 1, 1, 1, 1, record, (void *)0x1234) == 0); }
    for (i = 0; i < HALF; i++) assert(half[i] == 'L');
    /* Two slots is the smallest half it accepts, and it still clears only its
     * own slot. */
    {   struct koa2_marker_target least = { half, 2 * KOA2_MARKER_SLOT, 0x9be00000u };
        assert(koa2_marker_place(&least, 1, 1, 0, 0, record, (void *)0x1234));
        for (i = 0; i < KOA2_MARKER_SLOT; i++) assert(half[i] == 'L'); }
    assert(backs == 3);
}
int main(void)
{
    char out[KOA2_MARKER_MAX + 1];
    size_t n, i;
    memset(out, 0xa5, sizeof(out));
    n = koa2_marker_format(out, sizeof(out), 1, 42, 0x80d00000u, 0x81400000u);
    assert(n && n == strlen(out) && out[n] == 0);
    assert(!strcmp(out, "KOA2_HANDOFF v1 attempt=1 uptime=42s entry=0x80d00000 fdt=0x81400000\n"));
    /* The magic is first, so a grep finds it even if the rest is damaged. */
    assert(!strncmp(out, KOA2_MARKER_MAGIC, strlen(KOA2_MARKER_MAGIC)));
    /* Values that are easy to get wrong: zero, and the top of the range. */
    n = koa2_marker_format(out, sizeof(out), 0, 0, 0, 0);
    assert(n && !strcmp(out, "KOA2_HANDOFF v1 attempt=0 uptime=0s entry=0x00000000 fdt=0x00000000\n"));
    n = koa2_marker_format(out, sizeof(out), 4294967295u, 4294967295ul, 0xffffffffu, 0xffffffffu);
    assert(n && strstr(out, "attempt=4294967295") && strstr(out, "entry=0xffffffff"));
    assert(n <= KOA2_MARKER_MAX);
    /* Every buffer too small for the result is refused, and nothing is left
     * half-written in it: a truncated marker in the printk buffer would be
     * read as a different outcome than it is. */
    n = koa2_marker_format(out, sizeof(out), 1, 42, 0x80d00000u, 0x81400000u);
    for (i = 0; i <= n; i++) {
        char small[KOA2_MARKER_MAX + 1];
        memset(small, 0x5a, sizeof(small));
        assert(koa2_marker_format(small, i, 1, 42, 0x80d00000u, 0x81400000u) == 0);
        if (i >= 2) assert(small[0] == 0);
        /* Nothing past the size it was given was touched. */
        for (size_t j = i; j < sizeof(small); j++) assert(small[j] == 0x5a);
    }
    assert(koa2_marker_format(out, n + 1, 1, 42, 0x80d00000u, 0x81400000u) == n);
    /* Refusals. */
    assert(koa2_marker_format(NULL, sizeof(out), 1, 1, 1, 1) == 0);
    assert(koa2_marker_format(out, 0, 1, 1, 1, 1) == 0);
    assert(koa2_marker_format(out, 1, 1, 1, 1, 1) == 0);
    /* A buffer bigger than the marker can ever be is refused too: the caller
     * is handing over the printk buffer, and a wrong size there means a wrong
     * idea of where it ends. */
    { char big[KOA2_MARKER_MAX + 64];
      assert(koa2_marker_format(big, sizeof(big), 1, 1, 1, 1) == 0); }
    place();
    puts("KOA2 marker: the magic leads, every field reads back, no buffer too small is written past, "
         "and placing lands in the saved half, clears its slot and is always cleaned out");
    return 0;
}
