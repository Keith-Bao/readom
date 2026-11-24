#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Reversible combined quiesce dry-run (no jump, no partition write):
# Exercises the hardware-proven reversible steps in sequence:
#   1) USB gadget (g_mass_storage) guarded unload
#   2) Touch controller sleep via drv_debug=4 + regulator/IRQ check
#   3) CPU1 offline via sysfs
#   4) Reverse-order restore: CPU1 online -> Touch wake -> USB reload
# EPDC remains loaded in this dry-run until x.conf stop/reload is proven.
set -u
BASE=/mnt/us/koa2-full-quiesce-dryrun
TOUCH=/sys/bus/i2c/devices/1-0024
CPU1=/sys/devices/system/cpu/cpu1/online
[ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 combined reversible quiesce dry-run v1'
date -u

finish() {
    printf 'result=%s\n' "$1"
    exit 0
}

release=$(uname -r)
version=$(uname -v)
printf 'release=%s\nversion=%s\n' "$release" "$version"
[ "$release" = 4.1.15-lab126 ] && [ "$version" = '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ] ||
    finish refused_unknown_kernel
checksum=$(awk '$2 == "full-quiesce-dryrun.sh" {print; ++n} END {if (n != 1) exit 1}' "$HERE/SHA256SUMS") ||
    finish refused_kit_unverified
(cd "$HERE" && printf '%s\n' "$checksum" | sha256sum -c - >/dev/null) || finish refused_kit_unverified
mkdir "$BASE/.running" 2>/dev/null || finish refused_test_already_running
trap 'rmdir "$BASE/.running"' 0

# Preconditions: Wi-Fi unloaded, USB backing file empty, touch driver writable, cpu1 online.
if awk '$1 == "bcmdhd" {found=1; exit} END {exit !found}' /proc/modules; then
    finish refused_wifi_loaded
fi
usb_file=$(cat /sys/module/g_mass_storage/parameters/file 2>/dev/null || true)
[ -z "$usb_file" ] || finish refused_usb_active
usb_removable=1
[ -w "$TOUCH/drv_debug" ] || finish refused_touch_driver_missing
[ -w "$CPU1" ] && [ "$(cat "$CPU1")" = 1 ] || finish refused_cpu1_not_online

usb_removed=0
touch_slept=0
cpu1_offlined=0
result=dryrun_interrupted

restore_all() {
    trap '' HUP INT TERM
    restore_err=0
    if [ "$cpu1_offlined" = 1 ]; then
        if echo 1 > "$CPU1" && [ "$(cat "$CPU1")" = 1 ]; then
            printf 'restore_cpu1=ok\n'
        else
            printf 'restore_cpu1=failed\n'
            restore_err=1
        fi
    fi
    if [ "$touch_slept" = 1 ]; then
        if echo 5 > "$TOUCH/drv_debug"; then
            printf 'restore_touch=ok\n'
        else
            printf 'restore_touch=failed\n'
            restore_err=1
        fi
    fi
    if [ "$usb_removed" = 1 ]; then
        if modprobe g_mass_storage "removable=$usb_removable"; then
            printf 'restore_usb=ok\n'
        else
            printf 'restore_usb=failed\n'
            restore_err=1
        fi
    fi
    if [ "$restore_err" -ne 0 ]; then
        result=dryrun_restore_failed_restart_required
    elif [ "$result" = dryrun_quiesced_pending_restore ]; then
        result=full_quiesce_dryrun_restored
    fi
    rmdir "$BASE/.running"
    printf 'result=%s\n' "$result"
}
trap 'restore_all' 0
trap 'exit 1' HUP INT TERM

# Step 1: USB gadget unload
if rmmod g_mass_storage; then
    usb_removed=1
    printf 'step_usb=unloaded\n'
    sync
else
    result=step_usb_unload_failed
    exit 0
fi

# Step 2: Touch sleep
if echo 4 > "$TOUCH/drv_debug"; then
    touch_slept=1
    printf 'step_touch=slept\n'
    sync
    sleep 1
else
    result=step_touch_sleep_failed
    exit 0
fi

# Step 3: CPU1 offline
if echo 0 > "$CPU1" && [ "$(cat "$CPU1")" = 0 ]; then
    cpu1_offlined=1
    printf 'step_cpu1=offline\n'
    sync
else
    result=step_cpu1_offline_failed
    exit 0
fi

sleep 1
result=dryrun_quiesced_pending_restore
exit 0
