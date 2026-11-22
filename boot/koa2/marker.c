/* SPDX-License-Identifier: GPL-2.0-only */
#include "marker.h"
static size_t put_text(char *out, size_t max, size_t at, const char *text)
{
    while (*text) {
        if (at + 1 >= max) return 0;
        out[at++] = *text++;
    }
    return at;
}
static size_t put_decimal(char *out, size_t max, size_t at, unsigned long value)
{
    char digits[21];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) {
        if (at + 1 >= max) return 0;
        out[at++] = digits[--n];
    }
    return at;
}
static size_t put_hex(char *out, size_t max, size_t at, uint32_t value)
{
    int shift;
    at = put_text(out, max, at, "0x");
    if (!at) return 0;
    for (shift = 28; shift >= 0; shift -= 4) {
        if (at + 1 >= max) return 0;
        out[at++] = "0123456789abcdef"[(value >> shift) & 0xf];
    }
    return at;
}
size_t koa2_marker_format(char *out, size_t max, unsigned attempt,
                          unsigned long uptime_seconds, uint32_t entry, uint32_t fdt)
{
    size_t at = 0;
    if (!out || max < 2 || max > KOA2_MARKER_MAX + 1) return 0;
    at = put_text(out, max, at, KOA2_MARKER_MAGIC " v1 attempt=");
    if (at) at = put_decimal(out, max, at, attempt);
    if (at) at = put_text(out, max, at, " uptime=");
    if (at) at = put_decimal(out, max, at, uptime_seconds);
    if (at) at = put_text(out, max, at, "s entry=");
    if (at) at = put_hex(out, max, at, entry);
    if (at) at = put_text(out, max, at, " fdt=");
    if (at) at = put_hex(out, max, at, fdt);
    if (at) at = put_text(out, max, at, "\n");
    if (!at) { out[0] = 0; return 0; }
    out[at] = 0;
    return at;
}
size_t koa2_marker_place(const struct koa2_marker_target *target, unsigned attempt,
                         unsigned long uptime_seconds, uint32_t entry, uint32_t fdt,
                         void (*write_back)(void *context, char *va, uint32_t phys, size_t bytes),
                         void *context)
{
    char line[KOA2_MARKER_MAX + 1];
    char *slot;
    size_t n, i;
    /* No write-back means no way out of the caches, and the watchdog reset would
     * take the marker with it. Refuse rather than appear to have worked. */
    if (!target || !target->buffer || !write_back) return 0;
    /* The slot has to fit and leave log behind it: a half that is all marker is
     * a half with no log in it. */
    if (target->bytes < 2 * KOA2_MARKER_SLOT) return 0;
    /* The write-back is handed the slot's own physical address, derived from the
     * half's; refuse one that would wrap on the way. */
    if (target->phys > 0xffffffffu - (uint32_t)target->bytes) return 0;
    n = koa2_marker_format(line, sizeof(line), attempt, uptime_seconds, entry, fdt);
    if (!n || n > KOA2_MARKER_SLOT) return 0;
    slot = target->buffer + (target->bytes - KOA2_MARKER_SLOT);
    /* Clear, then write: a reader sees the marker and then nothing, rather than
     * the marker and then whatever was in the buffer before. */
    for (i = 0; i < KOA2_MARKER_SLOT; i++) slot[i] = 0;
    for (i = 0; i < n; i++) slot[i] = line[i];
    write_back(context, slot, target->phys + (uint32_t)(target->bytes - KOA2_MARKER_SLOT),
               KOA2_MARKER_SLOT);
    return n;
}
