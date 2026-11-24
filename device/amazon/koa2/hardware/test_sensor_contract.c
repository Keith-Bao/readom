/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "SensorContract.h"

int main(void) {
    int32_t x = 0, y = 0, z = 0;
    koa2_bma_remap_place1(100, -250, 980, &x, &y, &z);
    assert(x == 250 && y == 100 && z == 980);

    assert(koa2_orientation_deg(0, 1000, 180) == 0);
    assert(koa2_orientation_deg(0, -1000, 0) == 180);
    assert(koa2_orientation_deg(1000, 0, 0) == 90);
    assert(koa2_orientation_deg(-1000, 0, 0) == 270);
    /* Deadband preserves previous orientation (hysteresis). */
    assert(koa2_orientation_deg(500, 500, 180) == 180);
    assert(koa2_orientation_deg(100, 100, 90) == 90);

    /* Dual ALS fusion avoids dimming when one bezel sensor is covered. */
    assert(koa2_dual_als_lux(5, 180) == 180);
    assert(koa2_dual_als_lux(220, 10) == 220);
    assert(koa2_dual_als_lux(-1, 45) == 45);
    assert(koa2_dual_als_lux(30, -1) == 30);
    assert(koa2_dual_als_lux(-1, -1) == 0);

    printf("KOA2 sensor contract: BMA222E place=1 remap, 4-way orientation hysteresis, dual MAX44009 ALS fusion\n");
    return 0;
}
