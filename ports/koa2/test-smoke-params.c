/* SPDX-License-Identifier: GPL-2.0-only */
/* Host test for the smoke init's command line parsing. No device involved. */
#include "smoke-params.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    const char *key = "koa2_smoke_hold=";
    /* Absent, present, first word, last word. */
    assert(koa2_smoke_parameter("console=ttymxc0", key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("a koa2_smoke_hold=7 b", key, 20, 1, 50) == 7);
    assert(koa2_smoke_parameter("koa2_smoke_hold=7 b", key, 20, 1, 50) == 7);
    assert(koa2_smoke_parameter("a koa2_smoke_hold=7", key, 20, 1, 50) == 7);
    /* Out of range, non-numeric and empty values fall back. */
    assert(koa2_smoke_parameter("koa2_smoke_hold=0", key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=51", key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=4294967297", key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=x", key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=", key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=7x", key, 20, 1, 50) == 20);
    /* A longer option that merely ends with the key must not match. */
    assert(koa2_smoke_parameter("xkoa2_smoke_hold=7", key, 20, 1, 50) == 20);
    /* Boundaries are inclusive; the first usable occurrence wins. */
    assert(koa2_smoke_parameter("koa2_smoke_hold=1", key, 20, 1, 50) == 1);
    assert(koa2_smoke_parameter("koa2_smoke_hold=50", key, 20, 1, 50) == 50);
    assert(koa2_smoke_parameter("koa2_smoke_hold=99 koa2_smoke_hold=5", key, 20, 1, 50) == 5);
    /* Degenerate inputs. */
    assert(koa2_smoke_parameter(NULL, key, 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=7", "", 20, 1, 50) == 20);
    assert(koa2_smoke_parameter("koa2_smoke_hold=7", key, 20, 50, 1) == 20);
    puts("KOA2 smoke init command line parsing: 17 cases passed");
    return 0;
}
