/* SPDX-License-Identifier: GPL-2.0-only */
/* Kernel command line parsing for the KOA2 smoke init, split out so the host
 * tests can exercise it without a device (ports/koa2/test-smoke-params.c). */
#ifndef KOA2_SMOKE_PARAMS_H
#define KOA2_SMOKE_PARAMS_H
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
/* Reads "<key><digits>" from cmdline. Returns fallback unless the key appears
 * as its own word, the digits parse, and the value is within [low, high]. */
static inline unsigned koa2_smoke_parameter(const char *cmdline, const char *key,
                                            unsigned fallback, unsigned low, unsigned high) {
    size_t n = strlen(key);
    const char *p = cmdline;
    if (!cmdline || !n || low > high) return fallback;
    for (; (p = strstr(p, key)); p += n) {
        char *end;
        unsigned long v;
        if (p != cmdline && p[-1] != ' ' && p[-1] != '\t') continue;
        v = strtoul(p + n, &end, 10);
        if (end == p + n || (*end && *end != ' ' && *end != '\t')) continue;
        if (v < low || v > high) continue;
        return (unsigned)v;
    }
    return fallback;
}
#endif
