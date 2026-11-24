/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Kindle Oasis 2 (KOA2) accelerometer (BMA222E/BMA2x2) and dual-ALS (MAX44009)
 * hardware contract.
 *
 * Verified against stock runtime DTB (hwid=5):
 *  - bosch_accel@18: compatible="bosch,bma222e", bma,place=<1>.
 *  - max44009_als @ 0x4b and max44009_als1 @ 0x4a (dual ambient light sensors).
 *  - hall_sensor_disp: rohm,hall-bu520 (SW_LID).
 */
#ifndef KOA2_HARDWARE_SENSOR_CONTRACT_H
#define KOA2_HARDWARE_SENSOR_CONTRACT_H

#include <stdint.h>

#define KOA2_BMA_PLACE_RUNTIME 1

/* Bosch bma2x2 place=1 axis remapping (90 deg CW in XY plane, Z unchanged). */
static inline void koa2_bma_remap_place1(int32_t raw_x, int32_t raw_y, int32_t raw_z,
                                         int32_t *out_x, int32_t *out_y, int32_t *out_z) {
    *out_x = -raw_y;
    *out_y = raw_x;
    *out_z = raw_z;
}

/* Oasis 2 ergonomic 4-way orientation with hysteresis (returns 0, 90, 180, 270
 * degrees, or keeps prev_deg inside the deadband). Thresholds in milli-g. */
static inline int32_t koa2_orientation_deg(int32_t ax_mg, int32_t ay_mg, int32_t prev_deg) {
    const int32_t hi = 650;
    const int32_t lo = 350;
    int32_t abs_x = ax_mg < 0 ? -ax_mg : ax_mg;
    int32_t abs_y = ay_mg < 0 ? -ay_mg : ay_mg;
    if (abs_y >= hi && abs_x <= lo) {
        return ay_mg >= 0 ? 0 : 180;
    }
    if (abs_x >= hi && abs_y <= lo) {
        return ax_mg >= 0 ? 90 : 270;
    }
    return prev_deg;
}

/* Dual MAX44009 ALS fusion: take the higher valid non-negative reading so a
 * hand covering one bezel sensor does not spuriously dim the frontlight. */
static inline int32_t koa2_dual_als_lux(int32_t als0_lux, int32_t als1_lux) {
    if (als0_lux < 0 && als1_lux < 0) return 0;
    if (als0_lux < 0) return als1_lux;
    if (als1_lux < 0) return als0_lux;
    return als0_lux >= als1_lux ? als0_lux : als1_lux;
}

#endif
