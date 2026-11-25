// SPDX-License-Identifier: Apache-2.0
// Kindle Oasis 2 (KOA2 / MAX77796) healthd board adaptation.
#include <healthd/healthd.h>
#include <batteryservice/BatteryService.h>
#include "../hardware/PowerContract.h"

void healthd_board_init(healthd_config* config) {
    config->batteryChargeCounterPath =
        "/sys/class/power_supply/max77796-battery/charge_now";
    // Wake once per hour on battery; gauge interrupts report active changes.
    config->periodic_chores_interval_slow = 60 * 60;
}

int healthd_board_battery_update(android::BatteryProperties* props) {
    if (!props) return 0;
    // Clamp unclamped MAX77796 capacity (observed 103% on live KOA2 hardware).
    props->batteryLevel = koa2_battery_clamp_capacity(props->batteryLevel);
    return 0;
}
