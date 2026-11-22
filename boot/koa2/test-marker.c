/* SPDX-License-Identifier: GPL-2.0-only */
/* The marker is read by a person, out of a possibly damaged buffer, after the
 * device has reset. So it has to be plain, complete, and never run off the end
 * of the buffer it is given -- which is the printk buffer, and writing past it
 * would destroy the very log it exists to make readable. */
#include "marker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
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
    puts("KOA2 marker: the magic leads, every field reads back, and no buffer too small is written past");
    return 0;
}
