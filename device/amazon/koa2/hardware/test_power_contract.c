/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "PowerContract.h"

int main(void) {
    /* Observed live KOA2 stock snapshot: voltage=4118, current=-147, temp=271, capacity=103 */
    assert(koa2_battery_voltage_mv_to_uv(4118) == 4118000);
    assert(koa2_battery_voltage_mv_to_uv(-5) == 0);
    assert(koa2_battery_current_ma_to_ua(-147) == -147000);
    assert(koa2_battery_current_ma_to_ua(500) == 500000);
    assert(koa2_battery_charge_mah_to_uah(1000) == 1000000);
    assert(koa2_battery_charge_mah_to_uah(-1) == 0);
    assert(koa2_battery_clamp_capacity(103) == 100);
    assert(koa2_battery_clamp_capacity(0) == 0);
    assert(koa2_battery_clamp_capacity(-3) == 0);
    assert(koa2_battery_clamp_capacity(77) == 77);
    assert(koa2_battery_temp_tenths_c(271) == 271);

    /* Frontlight mapping 0..255 -> 0..4095, monotonic with exact endpoints */
    assert(koa2_brightness_android_to_hw(-1) == 0);
    assert(koa2_brightness_android_to_hw(0) == 0);
    assert(koa2_brightness_android_to_hw(255) == 4095);
    assert(koa2_brightness_android_to_hw(300) == 4095);
    int prev = -1;
    for (int i = 0; i <= 255; ++i) {
        int cur = koa2_brightness_android_to_hw(i);
        assert(cur >= prev);
        assert(cur >= 0 && cur <= 4095);
        prev = cur;
    }
    printf("KOA2 hardware power/backlight contract: battery unit scaling, capacity clamp, 12-bit frontlight\n");
    return 0;
}
