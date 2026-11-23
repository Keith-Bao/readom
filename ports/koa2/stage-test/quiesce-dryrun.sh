#!/bin/sh
# Inspect the stock system without unloading drivers or changing mount flags.
set -u
BASE=/mnt/us/koa2-quiesce-dryrun
[ -r /proc/version ] && [ -d /mnt/us/documents ] || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
LOG="$DIR/report.txt"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1

check_touch() {
    if [ ! -x "$HERE/touch-events" ]; then
        printf '%s\n' 'touch=touch_probe_missing'
        return
    fi
    if ! (cd "$HERE" && grep '  touch-events$' SHA256SUMS | sha256sum -c - >/dev/null 2>&1); then
        printf '%s\n' 'touch=touch_probe_checksum_failed'
        return
    fi
    for event in /sys/class/input/event*; do
        [ -r "$event/device/name" ] || continue
        [ "$(cat "$event/device/name")" = cyttsp5_mt ] || continue
        result=$("$HERE/touch-events" "/dev/input/${event##*/}")
        printf 'touch=%s\n' "${result:-touch_probe_failed}"
        return
    done
    printf '%s\n' 'touch=touch_device_missing'
}

{
    printf '%s\n' 'Readom KOA2 quiesce preflight v3'
    date -u
    uname -a
    if [ "$(uname -r)" != 4.1.15-lab126 ]; then
        printf '%s\n' 'result=refused_unknown_kernel'
    else
        printf '\n[system]\n'
        awk '$2 == "/" {print "rootfs=" $4; exit}' /proc/mounts
        printf 'cpu_online=%s\n' "$(cat /sys/devices/system/cpu/online 2>/dev/null)"

        printf '\n[drivers]\n'
        awk '$1 ~ /^(mxc_epdc_v2_fb|bcmdhd|g_mass_storage|usb_f_mass_storage|libcomposite|cyttsp5_i2c|bma2x2|als1_max44009)$/ {
            print "module=" $1, "users=" $3, "holders=" $4
        }' /proc/modules

        printf '\n[input]\n'
        for event in /sys/class/input/event*; do
            [ -r "$event/device/name" ] || continue
            printf 'input=%s name=%s\n' "${event##*/}" "$(cat "$event/device/name")"
        done
        for device in /sys/bus/i2c/drivers/cyttsp5_i2c_adapter/[0-9]*-[0-9a-f]*; do
            [ -d "$device" ] || continue
            printf 'touch_i2c_binding=%s\n' "${device##*/}"
        done
        if [ "${1:-}" = touch ]; then
            check_touch
        fi

        printf '\n[usb]\n'
        for parameter in file luns ro removable cdrom nofua stall recovery_mode; do
            value="/sys/module/g_mass_storage/parameters/$parameter"
            [ -r "$value" ] || continue
            if [ "$parameter" = file ]; then
                if [ -n "$(cat "$value")" ]; then
                    printf '%s\n' 'usb_backing_file=configured'
                else
                    printf '%s\n' 'usb_backing_file=empty'
                fi
            else
                printf 'usb_%s=%s\n' "$parameter" "$(cat "$value")"
            fi
        done
        for controller in /sys/class/udc/*; do
            [ -r "$controller/state" ] || continue
            printf 'usb_udc=%s state=%s\n' "${controller##*/}" "$(cat "$controller/state")"
        done

        printf '\n%s\n' 'result=quiesce_blocked_pending_restore'
    fi
} > "$LOG" || exit 1
printf '%s\n' "$RUN" > "$BASE/LAST_RUN.txt" || exit 1
printf 'Quiesce preflight report: %s\n' "$LOG"
