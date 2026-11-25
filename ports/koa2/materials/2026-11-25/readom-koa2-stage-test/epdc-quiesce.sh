#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# EPDC only: stop the whole X display tree through stock Upstart (the same path
# Amazon's dump-and-kill-cvm uses: `stop x` kills Xorg/awesome/blanket and
# releases the 3 framebuffer references), unload mxc_epdc_v2_fb, reload it with
# the exact waveform parameter it had, then start x and wait for Xorg to reopen
# /dev/fb0.
#
# 2026-11-25 offline audit findings from the stock rootfs:
#   x.conf: start on x_should_start, stop on (stopping dbus or stop_x); lxinit
#     spawns Xorg, awesome, blanket. post-stop kills all three.
#   lab126_gui.conf: start on (n_ready and langpicker_ready), stop on (stopping x
#     or ota-update). It owns the Java framework; stopping x cascades to it.
#   framework.conf: stop on stopping lab126_gui; owns cvm.
#   The initramfs and rootfs copies of mxc_epdc_v2_fb.ko are functionally
#   identical (same .text, .data, .rodata, __versions, .modinfo).
# Earlier (2026-11-25 device): stopping lab126_gui alone did not release the 3
# EPDC references because Xorg/awesome/blanket belong to the x job, not to
# lab126_gui.
set -u
BASE=/mnt/us/koa2-epdc-quiesce
MOD=mxc_epdc_v2_fb
[ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s
' 'Readom KOA2 EPDC-only stop and reload v2'
date -u
sync

finish() {
    printf 'result=%s
' "$1"
    sync
    exit 0
}

stage() {
    printf 'stage=%s uptime=%s
' "$1" "$(cut -d' ' -f1 /proc/uptime 2>/dev/null)"
    sync
}

mod_users() {
    awk -v m="$MOD" '$1 == m {print $3; found = 1; exit} END {exit !found}' /proc/modules
}

xorg_fb_open() {
    [ -e /dev/fb0 ] || return 1
    for process in /proc/[0-9]*; do
        [ "$(cat "$process/comm" 2>/dev/null)" = Xorg ] || continue
        for fd in "$process"/fd/*; do
            if [ "$fd" -ef /dev/fb0 ]; then
                return 0
            fi
        done
    done
    return 1
}

release=$(uname -r)
version=$(uname -v)
printf 'release=%s
version=%s
' "$release" "$version"
[ "$release" = 4.1.15-lab126 ] && [ "$version" = '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ] ||
    finish refused_unknown_kernel
checksum=$(awk '$2 == "epdc-quiesce.sh" {print; ++n} END {if (n != 1) exit 1}' "$HERE/SHA256SUMS") ||
    finish refused_kit_unverified
(cd "$HERE" && printf '%s
' "$checksum" | sha256sum -c - >/dev/null) || finish refused_kit_unverified
mkdir "$BASE/.running" 2>/dev/null || finish refused_test_already_running
trap 'rmdir "$BASE/.running"' 0

users_before=$(mod_users) || finish refused_epdc_not_loaded
MOD_KO=
for candidate in     "/lib/modules/$release/kernel/drivers/video/fbdev/mxc/$MOD.ko"     "/lib/modules/$MOD.ko"; do
    if [ -f "$candidate" ]; then
        MOD_KO=$candidate
        break
    fi
done
[ -n "$MOD_KO" ] || finish refused_epdc_ko_missing
waveform_file=/sys/module/$MOD/parameters/waveform
[ -r "$waveform_file" ] || finish refused_epdc_params_missing
waveform=$(tr -cd '[:alnum:]_.-' < "$waveform_file" 2>/dev/null)
[ -n "$waveform" ] || finish refused_epdc_params_missing
command -v initctl >/dev/null 2>&1 || finish refused_initctl_missing
x_status=$(initctl status x 2>/dev/null | head -n 1)
case "$x_status" in
    *"start/running"*) ;;
    *) finish refused_x_not_running ;;
esac
printf 'users_before=%s
mod_ko=%s
waveform=%s
x_status_before=%s
'     "$users_before" "$MOD_KO" "$waveform" "$x_status"

x_stopped=0
mod_unloaded=0
result=epdc_interrupted

restore_and_report() {
    trap '' HUP INT TERM
    reload_ok=1
    if [ "$mod_unloaded" = 1 ]; then
        stage restore_insmod
        if insmod "$MOD_KO" "waveform=$waveform"; then
            mod_unloaded=0
            printf 'epdc_reloaded=1
'
        else
            reload_ok=0
            printf 'epdc_reloaded=0
'
        fi
    fi
    x_ok=1
    if [ "$x_stopped" = 1 ]; then
        stage restore_x
        if initctl start x >/dev/null 2>&1; then
            x_stopped=0
            printf 'x_started=1
'
        else
            x_ok=0
            printf 'x_started=0
'
        fi
    fi
    gui_ok=1
    if [ "$reload_ok" = 1 ] && [ "$x_ok" = 1 ]; then
        waited=0
        while [ "$waited" -lt 30 ]; do
            if [ -r /sys/class/graphics/fb0/name ] && xorg_fb_open; then
                break
            fi
            sleep 1
            waited=$((waited + 1))
        done
        if [ -r /sys/class/graphics/fb0/name ] && xorg_fb_open; then
            printf 'fb_restored=1
'
            [ "$result" = epdc_unloaded_pending_restore ] && result=epdc_quiesce_restored
        else
            printf 'fb_restored=0
'
            [ "$result" = epdc_unloaded_pending_restore ] && result=epdc_gui_restore_timeout
        fi
    elif [ "$reload_ok" = 0 ]; then
        result=epdc_reload_failed_restart_required
    else
        result=epdc_x_start_failed_restart_required
    fi
    users_after=$(mod_users 2>/dev/null || printf 'missing')
    printf 'users_after=%s
' "$users_after"
    rmdir "$BASE/.running"
    stage done
    finish "$result"
}
trap 'restore_and_report' 0
# stop x sends SIGTERM to Xorg/awesome/blanket via its post-stop; ignore those
# so this script survives to unload, reload, and restart x.
trap '' HUP TERM
trap 'exit 1' INT

stage stop_x
x_stopped=1
if ! initctl stop x >/dev/null 2>&1; then
    x_stopped=0
    result=epdc_x_stop_failed
    exit 0
fi

# Wait for Xorg and its children to exit and close /dev/fb0.
waited=0
users_now=$users_before
while [ "$waited" -lt 15 ]; do
    users_now=$(mod_users 2>/dev/null || printf 'missing')
    [ "$users_now" = 0 ] && break
    sleep 1
    waited=$((waited + 1))
done
printf 'epdc_users_after_stop=%s wait=%s
' "$users_now" "$waited"
if [ "$users_now" != 0 ]; then
    result=epdc_users_not_released
    exit 0
fi

stage rmmod
if ! rmmod "$MOD"; then
    result=epdc_rmmod_failed
    exit 0
fi
mod_unloaded=1
if mod_users >/dev/null 2>&1 || [ -e /sys/class/graphics/fb0 ]; then
    printf 'epdc_unloaded=0
'
    result=epdc_unload_unconfirmed
    exit 0
fi
printf 'epdc_unloaded=1
'
result=epdc_unloaded_pending_restore
exit 0
