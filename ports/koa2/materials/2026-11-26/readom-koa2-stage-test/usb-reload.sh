#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Run from KUAL with the USB cable physically disconnected.
set -u
BASE=/mnt/us/koa2-usb-reload
UDC=/sys/class/udc/ci_hdrc.0
LUN="$UDC/device/gadget/lun0/file"
PARAM=/sys/module/g_mass_storage/parameters
[ -d /mnt/us/documents ] || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 USB-only reload v1'
date -u

finish() {
    printf 'result=%s\n' "$1"
    exit 0
}

module_users() {
    awk '$1 == "g_mass_storage" {print $3; exit}' /proc/modules
}

expect() {
    if value=$(cat "$1") && [ "$value" = "$2" ]; then
        return 0
    fi
    printf 'usb_state_mismatch=%s\n' "$1"
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

release=$(uname -r)
version=$(uname -v)
printf 'release=%s\nversion=%s\n' "$release" "$version"
[ "$release" = 4.1.15-lab126 ] && [ "$version" = '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ] ||
    finish refused_unknown_kernel
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || finish refused_kit_unreadable
(cd "$HERE" && grep '  usb-reload.sh$' SHA256SUMS | sha256sum -c -) || finish refused_kit_unverified

mkdir "$BASE/.running" 2>/dev/null || finish refused_test_already_running
trap 'rmdir "$BASE/.running"' 0
[ "$(module_users)" = 0 ] || finish refused_module_missing_or_busy
stock_parameters || finish refused_unexpected_storage_parameters
idle_lun || finish refused_lun_not_proven_idle

# Preserve numeric USB identity overrides. Skip NULL/empty string defaults,
# and refuse custom strings until their modprobe quoting can be verified.
set -- removable=1
for option in idVendor idProduct bcdDevice num_buffers; do
    if [ "$option" = num_buffers ] && [ ! -e "$PARAM/$option" ]; then
        continue
    fi
    value=$(cat "$PARAM/$option") || finish refused_unreadable_usb_descriptor
    printf '%s\n' "$value" | grep -Eq '^[0-9]{1,5}$' || finish refused_unexpected_usb_descriptor
    set -- "$@" "$option=$value"
done
for option in iManufacturer iProduct iSerialNumber; do
    value=$(cat "$PARAM/$option") || finish refused_unreadable_usb_descriptor
    case "$value" in ''|'(null)') ;; *) finish refused_custom_usb_strings ;; esac
done

# On signals, finish the single restoration attempt before releasing the lock.
result=usb_interrupted
restore_and_report() {
    trap '' HUP INT TERM
    if [ -z "$(module_users)" ]; then
        modprobe g_mass_storage "$@" || result=usb_reload_failed_restart_required
    fi
    if [ "$result" != usb_reload_failed_restart_required ]; then
        if [ "$(module_users)" != 0 ] || ! stock_parameters || ! idle_lun; then
            result=usb_rebind_unverified_restart_required
        else
            for argument do
                [ "$argument" = removable=1 ] && continue
                if ! expect "$PARAM/${argument%%=*}" "${argument#*=}"; then
                    result=usb_rebind_unverified_restart_required
                fi
            done
        fi
    fi
    rmdir "$BASE/.running"
    printf 'result=%s\n' "$result"
}

idle_lun && [ "$(module_users)" = 0 ] || finish refused_lun_not_proven_idle
trap 'restore_and_report "$@"' 0
trap 'exit 1' HUP INT TERM
if rmmod g_mass_storage; then
    if [ -z "$(module_users)" ]; then
        printf '%s\n' 'usb_module_removed=1'
        result=usb_idle_rebound_host_check_pending
    else
        result=usb_removal_not_observed
    fi
else
    result=usb_remove_failed
fi
exit 0
