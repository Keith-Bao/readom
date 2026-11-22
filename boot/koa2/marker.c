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
