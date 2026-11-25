#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Combined reversible quiesce dry-run: no jump, no partition write, EPDC left
# alone. It chains the three steps the device has passed one at a time, each
# behind the same guards as its own script:
#   1) USB gadget unload  (usb-reload.sh: idle LUN, stock parameters)
#   2) Touch stock sleep  (touch-quiesce.sh: drv_debug=4, supply checked off)
#   3) CPU1 offline       (cpu1-quiesce.sh: sysfs hotplug, 0-1 -> 0)
# and restores them in reverse: CPU1 online, touch wake (drv_debug=5), USB
# reload with the numeric descriptors it had before.
# usage: full-quiesce-dryrun.sh [STEPS]  STEPS is a comma list in this order,
# default usb,touch,cpu1. A pair (usb,touch or touch,cpu1) narrows down which
# combination stops the device if the full run freezes.
# v1 froze the Kindle on 2026-11-25 and its report never reached the disk.
# So before every action this script writes and syncs a stage= line: after a
# freeze, the last stage= line names the step that did not return. After such
# a restart, run "Read previous boot recovery log" before anything else.
set -u
BASE=/mnt/us/koa2-full-quiesce-dryrun
TOUCH=/sys/bus/i2c/devices/1-0024
CPU_ONLINE=/sys/devices/system/cpu/online
CPU1=/sys/devices/system/cpu/cpu1/online
UDC=/sys/class/udc/ci_hdrc.0
LUN="$UDC/device/gadget/lun0/file"
PARAM=/sys/module/g_mass_storage/parameters
[ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 combined reversible quiesce dry-run v2'
date -u
sync

finish() {
    printf 'result=%s\n' "$1"
    sync
    exit 0
}

# Written and flushed before each action, so a report cut short by a freeze
# still names the step in progress.
stage() {
    printf 'stage=%s uptime=%s cpus=%s\n' "$1" \
        "$(cut -d' ' -f1 /proc/uptime 2>/dev/null)" "$(cat "$CPU_ONLINE" 2>/dev/null)"
    sync
}

module_users() {
    awk '$1 == "g_mass_storage" {print $3; exit}' /proc/modules
}

expect() {
    if value=$(cat "$1") && [ "$value" = "$2" ]; then
        return 0
    fi
    printf 'state_mismatch=%s\n' "$1"
    return 1
}

idle_lun() {
    expect "$UDC/state" 0 && [ ! -e "$UDC/connected" ] && expect "$LUN" ''
}

stock_parameters() {
    expect "$PARAM/file" '' && expect "$PARAM/luns" 0 &&
        expect "$PARAM/removable" Y && expect "$PARAM/stall" Y &&
        expect "$PARAM/recovery_mode" 0 && expect "$PARAM/ro" '' &&
        expect "$PARAM/cdrom" '' && expect "$PARAM/nofua" ''
}

regulator_state() {
    for regulator in /sys/class/regulator/*; do
        [ "$(cat "$regulator/name" 2>/dev/null)" = MAX77796-LSW1 ] || continue
        cat "$regulator/state"
        return
    done
    return 1
}

# "on_enabled" awake, "off_disabled" asleep and unpowered. sleep_status alone
# reads off even when the power-down failed (touch-quiesce.sh), so the
# regulator is read with it.
touch_power() {
    printf '%s_%s\n' "$(cat "$TOUCH/sleep_status" 2>/dev/null)" "$(regulator_state 2>/dev/null)"
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
STEPS=${1:-usb,touch,cpu1}
case "$STEPS" in
    usb,touch,cpu1|usb,touch|usb,cpu1|touch,cpu1|usb|touch|cpu1) ;;
    *) finish refused_unknown_steps ;;
esac
printf 'steps=%s\n' "$STEPS"
has() {
    case ",$STEPS," in *",$1,"*) return 0 ;; esac
    return 1
}

# Preconditions, each as its own script checks it. Nothing is written yet.
if awk '$1 == "bcmdhd" {found=1; exit} END {exit !found}' /proc/modules; then
    finish refused_wifi_loaded
fi
if has usb; then
    [ "$(module_users)" = 0 ] || finish refused_usb_module_missing_or_busy
    stock_parameters || finish refused_unexpected_storage_parameters
    idle_lun || finish refused_lun_not_proven_idle
    # The reload keeps the numeric USB identity it had; custom strings are refused
    # because their modprobe quoting has not been verified (usb-reload.sh).
    usb_args=removable=1
    for option in idVendor idProduct bcdDevice num_buffers; do
        if [ "$option" = num_buffers ] && [ ! -e "$PARAM/$option" ]; then
            continue
        fi
        value=$(cat "$PARAM/$option") || finish refused_unreadable_usb_descriptor
        printf '%s\n' "$value" | grep -Eq '^[0-9]{1,5}$' || finish refused_unexpected_usb_descriptor
        usb_args="$usb_args $option=$value"
    done
    for option in iManufacturer iProduct iSerialNumber; do
        value=$(cat "$PARAM/$option") || finish refused_unreadable_usb_descriptor
        case "$value" in ''|'(null)') ;; *) finish refused_custom_usb_strings ;; esac
    done
    printf 'usb_reload_args=%s\n' "$usb_args"
fi
if has touch; then
    [ -e "$TOUCH/driver" ] && [ -w "$TOUCH/drv_debug" ] || finish refused_touch_driver_missing
    power=$(touch_power)
    printf 'touch_power_before=%s\n' "$power"
    [ "$power" = on_enabled ] || finish refused_touch_not_awake
fi
if has cpu1; then
    [ -w "$CPU1" ] && [ "$(cat "$CPU_ONLINE")" = 0-1 ] && [ "$(cat "$CPU1")" = 1 ] ||
        finish refused_cpu1_not_online
fi

usb_removed=0
touch_slept=0
cpu1_offlined=0
result=dryrun_interrupted

# One restore attempt for every step that ran, newest first, each checked the
# way its own script checks it.
restore_all() {
    trap '' HUP INT TERM
    restore_err=0
    if [ "$cpu1_offlined" = 1 ]; then
        stage restore_cpu1
        echo 1 > "$CPU1"
        if [ "$(cat "$CPU1")" = 1 ] && [ "$(cat "$CPU_ONLINE")" = 0-1 ]; then
            printf 'restore_cpu1=ok\n'
        else
            printf 'restore_cpu1=failed\n'
            restore_err=1
        fi
    fi
    if [ "$touch_slept" = 1 ]; then
        stage restore_touch
        echo 5 > "$TOUCH/drv_debug"
        sleep 1
        power=$(touch_power)
        printf 'touch_power_restored=%s\n' "$power"
        if [ "$power" = on_enabled ]; then
            printf 'restore_touch=ok\n'
        else
            printf 'restore_touch=failed\n'
            restore_err=1
        fi
    fi
    if [ "$usb_removed" = 1 ]; then
        stage restore_usb
        if [ -z "$(module_users)" ]; then
            # $usb_args holds only the numeric options checked above.
            modprobe g_mass_storage $usb_args
        fi
        usb_ok=1
        [ "$(module_users)" = 0 ] && stock_parameters && idle_lun || usb_ok=0
        for argument in $usb_args; do
            [ "$argument" = removable=1 ] && continue
            expect "$PARAM/${argument%%=*}" "${argument#*=}" || usb_ok=0
        done
        if [ "$usb_ok" = 1 ]; then
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
    stage done
    finish "$result"
}
trap 'restore_all' 0
trap 'exit 1' HUP INT TERM

# Step 1: USB gadget unload.
if has usb; then
    stage usb_unload
    usb_removed=1
    rmmod g_mass_storage
    if [ -n "$(module_users)" ]; then
        result=step_usb_unload_failed
        exit 0
    fi
    printf 'step_usb=unloaded\n'
fi

# Step 2: touch sleep through the stock driver, then let an interrupt handler
# already running finish before CPU1 is touched.
if has touch; then
    stage touch_sleep
    touch_slept=1
    echo 4 > "$TOUCH/drv_debug" || { result=step_touch_sleep_failed; exit 0; }
    sleep 1
    power=$(touch_power)
    printf 'touch_power_asleep=%s\n' "$power"
    [ "$power" = off_disabled ] || { result=step_touch_not_powered_down; exit 0; }
    printf 'step_touch=slept\n'
fi

# Step 3: CPU1 offline through stock hotplug.
if has cpu1; then
    stage cpu1_offline
    cpu1_offlined=1
    echo 0 > "$CPU1"
    if [ "$(cat "$CPU1")" != 0 ] || [ "$(cat "$CPU_ONLINE")" != 0 ]; then
        result=step_cpu1_offline_failed
        exit 0
    fi
    printf 'step_cpu1=offline\n'
fi

# Hold all three for a moment, then confirm nothing came back on its own.
stage hold
sleep 1
if has cpu1 && [ "$(cat "$CPU_ONLINE")" != 0 ]; then
    result=step_cpu1_came_back_early
elif has touch && [ "$(touch_power)" != off_disabled ]; then
    result=step_touch_woke_early
else
    result=dryrun_quiesced_pending_restore
fi
exit 0
