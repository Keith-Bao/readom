/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Kindle Oasis 2 (MAX77796) battery and frontlight unit-conversion contract.
 *
 * Verified against stock Linux 4.1.15 drivers/power/max77696-battery.c:
 *  - POWER_SUPPLY_PROP_VOLTAGE_NOW : millivolts (mV)  -> Android healthd needs uV (x1000)
 *  - POWER_SUPPLY_PROP_CURRENT_NOW : milliamps  (mA)  -> Android healthd needs uA (x1000)
 *  - POWER_SUPPLY_PROP_CHARGE_NOW  : mAh              -> Android healthd needs uAh (x1000)
 *  - POWER_SUPPLY_PROP_TEMP        : 0.1 deg C        -> already matches Android healthd
 *  - POWER_SUPPLY_PROP_CAPACITY    : unclamped % (observed 103 on device) -> clamp [0, 100]
 *
 * Verified against drivers/video/backlight/max77696-bl.c and hardware-findings.md:
 *  - /sys/class/backlight/max77796-bl/max_brightness = 4095 (12-bit).
 */
#ifndef KOA2_HARDWARE_POWER_CONTRACT_H
#define KOA2_HARDWARE_POWER_CONTRACT_H

#include <stdint.h>

#define KOA2_BACKLIGHT_MAX 4095
#define KOA2_ANDROID_BRIGHTNESS_MAX 255

static inline int32_t koa2_battery_voltage_mv_to_uv(int32_t mv) {
    if (mv < 0) return 0;
    if (mv > 20000) return 20000 * 1000;
    return mv * 1000;
}

static inline int32_t koa2_battery_current_ma_to_ua(int32_t ma) {
    if (ma < -20000) return -20000 * 1000;
    if (ma > 20000) return 20000 * 1000;
    return ma * 1000;
}

static inline int32_t koa2_battery_charge_mah_to_uah(int32_t mah) {
    if (mah < 0) return 0;
    if (mah > 50000) return 50000 * 1000;
    return mah * 1000;
}

static inline int32_t koa2_battery_clamp_capacity(int32_t raw_percent) {
    if (raw_percent < 0) return 0;
    if (raw_percent > 100) return 100;
    return raw_percent;
}

static inline int32_t koa2_battery_temp_tenths_c(int32_t raw_tenths_c) {
    return raw_tenths_c;
}

static inline int32_t koa2_brightness_android_to_hw(int32_t level) {
    if (level <= 0) return 0;
    if (level >= KOA2_ANDROID_BRIGHTNESS_MAX) return KOA2_BACKLIGHT_MAX;
    return (level * KOA2_BACKLIGHT_MAX + (KOA2_ANDROID_BRIGHTNESS_MAX / 2)) /
           KOA2_ANDROID_BRIGHTNESS_MAX;
}

#endif
