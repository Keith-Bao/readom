#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# EPDC only: release framebuffer users via stock Upstart GUI job, unload
# mxc_epdc_v2_fb, reload it with its exact saved waveform parameter, and
# restart the GUI job. Host-tested only; not added to menu.json until
# read-only epdc_service_* and epdc_param_* values are confirmed on hardware.
set -u
BASE=/mnt/us/koa2-epdc-quiesce
MOD=mxc_epdc_v2_fb
GUI_JOB=${KOA2_EPDC_GUI_JOB:-lab126_gui}
[ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 EPDC-only stop and reload v1'
date -u

finish() {
    printf 'result=%s\n' "$1"
    exit 0
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
printf 'release=%s\nversion=%s\n' "$release" "$version"
[ "$release" = 4.1.15-lab126 ] && [ "$version" = '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ] ||
    finish refused_unknown_kernel
checksum=$(awk '$2 == "epdc-quiesce.sh" {print; ++n} END {if (n != 1) exit 1}' "$HERE/SHA256SUMS") ||
    finish refused_kit_unverified
(cd "$HERE" && printf '%s\n' "$checksum" | sha256sum -c - >/dev/null) || finish refused_kit_unverified
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
command -v initctl >/dev/null 2>&1 || finish refused_gui_service_missing
gui_status=$(initctl status "$GUI_JOB" 2>/dev/null | head -n 1)
case "$gui_status" in
    *"start/running"*) ;;
    *) finish refused_gui_service_missing ;;
esac
printf 'users_before=%s\nmod_ko=%s\nwaveform=%s\ngui_job=%s\n' "$users_before" "$MOD_KO" "$waveform" "$GUI_JOB"

gui_stopped=0
mod_unloaded=0
result=epdc_interrupted

restore_and_report() {
    trap '' HUP INT TERM
    reload_ok=1
    if [ "$mod_unloaded" = 1 ]; then
        if insmod "$MOD_KO" "waveform=$waveform"; then
            mod_unloaded=0
            printf 'epdc_reloaded=1\n'
        else
            reload_ok=0
            printf 'epdc_reloaded=0\n'
        fi
    fi
    gui_ok=1
    if [ "$gui_stopped" = 1 ]; then
        if initctl start "$GUI_JOB" >/dev/null 2>&1; then
            gui_stopped=0
            printf 'gui_started=1\n'
        else
            gui_ok=0
            printf 'gui_started=0\n'
        fi
    fi
    if [ "$reload_ok" = 1 ] && [ "$gui_ok" = 1 ]; then
        waited=0
        while [ "$waited" -lt 20 ]; do
            if [ -r /sys/class/graphics/fb0/name ] && xorg_fb_open; then
                break
            fi
            sleep 1
            waited=$((waited + 1))
        done
        if [ -r /sys/class/graphics/fb0/name ] && xorg_fb_open; then
            printf 'fb_restored=1\n'
            [ "$result" = epdc_unloaded_pending_restore ] && result=epdc_quiesce_restored
        else
            printf 'fb_restored=0\n'
            [ "$result" = epdc_unloaded_pending_restore ] && result=epdc_gui_restore_failed_restart_required
        fi
    elif [ "$reload_ok" = 0 ]; then
        result=epdc_reload_failed_restart_required
    else
        result=epdc_gui_restore_failed_restart_required
    fi
    users_after=$(mod_users 2>/dev/null || printf 'missing')
    printf 'users_after=%s\n' "$users_after"
    rmdir "$BASE/.running"
    printf 'result=%s\n' "$result"
}
trap 'restore_and_report' 0
# Stopping lab126_gui sends SIGHUP/SIGTERM to processes in the GUI tree; ignore
# both so the script survives to unload, reload, and restart lab126_gui.
trap '' HUP TERM
trap 'exit 1' INT

gui_stopped=1
if ! initctl stop "$GUI_JOB" >/dev/null 2>&1; then
    gui_stopped=0
    result=epdc_gui_stop_failed
    exit 0
fi

waited=0
users_now=$users_before
while [ "$waited" -lt 10 ]; do
    users_now=$(mod_users 2>/dev/null || printf 'missing')
    [ "$users_now" = 0 ] && break
    sleep 1
    waited=$((waited + 1))
done
printf 'epdc_users_after_stop=%s\n' "$users_now"
if [ "$users_now" != 0 ]; then
    result=epdc_users_not_released
    exit 0
fi

if ! rmmod "$MOD"; then
    result=epdc_rmmod_failed
    exit 0
fi
mod_unloaded=1
if mod_users >/dev/null 2>&1 || [ -e /sys/class/graphics/fb0 ]; then
    printf 'epdc_unloaded=0\n'
    result=epdc_unload_unconfirmed
    exit 0
fi
printf 'epdc_unloaded=1\n'
result=epdc_unloaded_pending_restore
exit 0
