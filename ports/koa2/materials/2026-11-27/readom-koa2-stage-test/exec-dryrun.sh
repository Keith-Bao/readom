#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Exec dry-run: quiesce, arm the trampoline, stop before the caches. NO jump.
# It chains the reversible steps already proven on this device, each behind the
# guards of its own script:
#   1) EPDC: initctl stop x, rmmod mxc_epdc_v2_fb    (epdc-quiesce.sh v4)
#   2) USB: verify the unplugged UDC is idle, read-only (full-quiesce-dryrun.sh v4)
#   3) Touch: stock sleep, drv_debug=4, supply off    (touch-quiesce.sh)
#   4) CPU1: offline through sysfs hotplug            (cpu1-quiesce.sh)
#   5) sync; record whether / is read-only (nothing is remounted)
#   6) insmod koa2_handoff_exec.ko; koa2-stage-client --exec-dryrun stages,
#      seals, and asks the module to quiesce-verify, re-audit the copy list,
#      arm the trampoline in a page it owns and read SCTLR/TTBCR with IRQ/FIQ
#      masked. The module stops before disabling the caches and gives the page
#      back; rmmod.
# then restores in reverse: CPU1 online, touch wake, EPDC reload with its own
# waveform, start x, start lab126_gui. The watchdog is not touched.
# Before every action a stage= line is written and synced: after a freeze the
# last one names the step that did not return. After such a restart, run
# "Read previous boot recovery log" before anything else.
set -u
BASE=/mnt/us/koa2-exec-dryrun
TOUCH=/sys/bus/i2c/devices/1-0024
CPU_ONLINE=/sys/devices/system/cpu/online
CPU1=/sys/devices/system/cpu/cpu1/online
UDC=/sys/class/udc/ci_hdrc.0
EPDC=mxc_epdc_v2_fb
EXEC=koa2_handoff_exec
[ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 exec dry-run v1 -- arms the trampoline, stops before caches, NO jump'
date -u
sync

finish() {
    printf 'result=%s\n' "$1"
    sync
    exit 0
}
stage() {
    printf 'stage=%s uptime=%s cpus=%s\n' "$1" \
        "$(cut -d' ' -f1 /proc/uptime 2>/dev/null)" "$(cat "$CPU_ONLINE" 2>/dev/null)"
    sync
}
loaded() { awk -v m="$1" '$1 == m {f=1; exit} END {exit !f}' /proc/modules; }
users_of() { awk -v m="$1" '$1 == m {print $3; f=1; exit} END {exit !f}' /proc/modules; }
regulator_state() {
    for regulator in /sys/class/regulator/*; do
        [ "$(cat "$regulator/name" 2>/dev/null)" = MAX77796-LSW1 ] || continue
        cat "$regulator/state"
        return
    done
    return 1
}
touch_power() {
    printf '%s_%s\n' "$(cat "$TOUCH/sleep_status" 2>/dev/null)" "$(regulator_state 2>/dev/null)"
}
xorg_fb_open() {
    [ -e /dev/fb0 ] || return 1
    for process in /proc/[0-9]*; do
        [ "$(cat "$process/comm" 2>/dev/null)" = Xorg ] || continue
        for fd in "$process"/fd/*; do
            [ "$fd" -ef /dev/fb0 ] && return 0
        done
    done
    return 1
}

release=$(uname -r)
version=$(uname -v)
printf 'release=%s\nversion=%s\n' "$release" "$version"
[ "$release" = 4.1.15-lab126 ] && [ "$version" = '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ] ||
    finish refused_unknown_kernel
# The whole kit, module and client included: a damaged copy refuses here.
(cd "$HERE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || finish refused_kit_unverified
mkdir "$BASE/.running" 2>/dev/null || finish refused_test_already_running
trap 'rmdir "$BASE/.running"' 0

# Preconditions. Nothing is changed until all of them hold.
loaded bcmdhd && finish refused_wifi_loaded
loaded "$EXEC" && finish refused_exec_module_already_loaded
loaded koa2_handoff_stage && finish refused_stage_module_loaded
users_of "$EPDC" >/dev/null || finish refused_epdc_not_loaded
EPDC_KO=
for candidate in "/lib/modules/$release/kernel/drivers/video/fbdev/mxc/$EPDC.ko" "/lib/modules/$EPDC.ko"; do
    [ -f "$candidate" ] && { EPDC_KO=$candidate; break; }
done
[ -n "$EPDC_KO" ] || finish refused_epdc_ko_missing
waveform=$(tr -cd '[:alnum:]_.-' < "/sys/module/$EPDC/parameters/waveform" 2>/dev/null)
[ -n "$waveform" ] || finish refused_epdc_params_missing
command -v initctl >/dev/null 2>&1 || finish refused_initctl_missing
case "$(initctl status x 2>/dev/null | head -n 1)" in
    *"start/running"*) ;;
    *) finish refused_x_not_running ;;
esac
[ "$(users_of g_mass_storage)" = 0 ] || finish refused_usb_module_missing_or_busy
udc_before=$(cat "$UDC/state" 2>/dev/null)
case "$udc_before" in configured|1) finish refused_usb_host_connected ;; esac
[ -e "$TOUCH/driver" ] && [ -w "$TOUCH/drv_debug" ] || finish refused_touch_driver_missing
[ "$(touch_power)" = on_enabled ] || finish refused_touch_not_awake
[ -w "$CPU1" ] && [ "$(cat "$CPU_ONLINE")" = 0-1 ] && [ "$(cat "$CPU1")" = 1 ] ||
    finish refused_cpu1_not_online
printf 'epdc_ko=%s\nwaveform=%s\nudc_state_before=%s\n' "$EPDC_KO" "$waveform" "$udc_before"

x_stopped=0
epdc_unloaded=0
touch_slept=0
cpu1_offlined=0
exec_loaded=0
result=exec_interrupted

# One attempt per step that ran, newest first, each checked as its own script
# checks it. The execute module is unloaded first: closing its device already
# freed every staged page, and rmmod takes nothing else with it.
restore_all() {
    trap '' HUP INT TERM
    restore_err=0
    if [ "$exec_loaded" = 1 ]; then
        stage restore_rmmod_exec
        if rmmod "$EXEC"; then printf 'exec_unloaded=1\n'; else printf 'exec_unloaded=0\n'; restore_err=1; fi
    fi
    if [ "$cpu1_offlined" = 1 ]; then
        stage restore_cpu1
        echo 1 > "$CPU1"
        if [ "$(cat "$CPU1")" = 1 ] && [ "$(cat "$CPU_ONLINE")" = 0-1 ]; then
            printf 'restore_cpu1=ok\n'
        else
            printf 'restore_cpu1=failed\n'; restore_err=1
        fi
    fi
    if [ "$touch_slept" = 1 ]; then
        stage restore_touch
        echo 5 > "$TOUCH/drv_debug"
        sleep 1
        if [ "$(touch_power)" = on_enabled ]; then
            printf 'restore_touch=ok\n'
        else
            printf 'restore_touch=failed power=%s\n' "$(touch_power)"; restore_err=1
        fi
    fi
    if [ "$epdc_unloaded" = 1 ]; then
        stage restore_insmod_epdc
        if insmod "$EPDC_KO" "waveform=$waveform"; then
            printf 'epdc_reloaded=1\n'
        else
            printf 'epdc_reloaded=0\n'; restore_err=1
        fi
    fi
    if [ "$x_stopped" = 1 ]; then
        stage restore_x
        if initctl start --no-wait x >/dev/null 2>&1; then
            waited=0
            while [ "$waited" -lt 30 ]; do
                [ -r /sys/class/graphics/fb0/name ] && xorg_fb_open && break
                sleep 1; waited=$((waited + 1))
            done
            if [ -r /sys/class/graphics/fb0/name ] && xorg_fb_open; then
                printf 'fb_restored=1\n'
                case "$(initctl status lab126_gui 2>/dev/null | head -n 1)" in
                    *"start/running"*) ;;
                    *) initctl start lab126_gui --no-wait >/dev/null 2>&1 || true ;;
                esac
            else
                printf 'fb_restored=0\n'; restore_err=1
            fi
        else
            printf 'x_started=0\n'; restore_err=1
        fi
        printf 'x_status_after=%s\nlab126_gui_status_after=%s\n' \
            "$(initctl status x 2>/dev/null | head -n 1)" \
            "$(initctl status lab126_gui 2>/dev/null | head -n 1)"
    fi
    if [ "$restore_err" -ne 0 ]; then
        result=exec_restore_failed_restart_required
    else
        case "$result" in
            exec_armed_pending_restore) result=exec_dryrun_restored ;;
            exec_not_quiesced_pending_restore) result=exec_dryrun_restored_not_quiesced ;;
        esac
    fi
    rmdir "$BASE/.running"
    stage done
    finish "$result"
}
trap 'restore_all' 0
# stop x signals the X process tree; ignore HUP/TERM so this script survives
# to restore everything (epdc-quiesce.sh v4).
trap '' HUP TERM
trap 'exit 1' INT

# Step 1: EPDC. Stop x, wait for the three /dev/fb0 users to go, unload.
stage stop_x
x_stopped=1
initctl stop x >/dev/null 2>&1 || { x_stopped=0; result=step_x_stop_failed; exit 0; }
waited=0
while [ "$waited" -lt 15 ]; do
    [ "$(users_of "$EPDC" 2>/dev/null)" = 0 ] && break
    sleep 1; waited=$((waited + 1))
done
printf 'epdc_users_after_stop=%s wait=%s\n' "$(users_of "$EPDC" 2>/dev/null)" "$waited"
[ "$(users_of "$EPDC" 2>/dev/null)" = 0 ] || { result=step_epdc_users_not_released; exit 0; }
stage rmmod_epdc
rmmod "$EPDC" || { result=step_epdc_rmmod_failed; exit 0; }
epdc_unloaded=1
if loaded "$EPDC" || [ -e /sys/class/graphics/fb0 ]; then result=step_epdc_unload_unconfirmed; exit 0; fi
printf 'step_epdc=unloaded\n'

# Step 2: USB. Unplugged, the stock driver already stopped the controller and
# gated its clock; touching rmmod or soft_connect hangs the bus. Read only.
stage usb_verify_idle
case "$(cat "$UDC/state" 2>/dev/null)" in configured|1) result=step_usb_not_idle; exit 0 ;; esac
printf 'step_usb=quiesced_idle\n'

# Step 3: touch sleep through the stock driver; let a running handler finish.
stage touch_sleep
touch_slept=1
echo 4 > "$TOUCH/drv_debug" || { result=step_touch_sleep_failed; exit 0; }
sleep 1
[ "$(touch_power)" = off_disabled ] || { result=step_touch_not_powered_down; exit 0; }
printf 'step_touch=slept\n'

# Step 4: CPU1 offline through stock hotplug.
stage cpu1_offline
cpu1_offlined=1
echo 0 > "$CPU1"
[ "$(cat "$CPU1")" = 0 ] && [ "$(cat "$CPU_ONLINE")" = 0 ] || { result=step_cpu1_offline_failed; exit 0; }
printf 'step_cpu1=offline\n'

# Step 5: flush, and record whether / is read-only. Nothing is remounted.
stage sync
sync
rootfs_ro=0
awk '$2 == "/" {print "rootfs_mount=" $4}' /proc/mounts
awk '$2 == "/" && $4 ~ /(^|,)ro(,|$)/ {f=1} END {exit !f}' /proc/mounts && rootfs_ro=1
printf 'rootfs_readonly=%s\n' "$rootfs_ro"
writable=$(awk '$4 ~ /(^|,)rw(,|$)/ && $1 ~ /^\/dev\// {printf "%s ", $2}' /proc/mounts)
printf 'writable_block_mounts=%s\n' "${writable:-none}"

# The claim the module checks, counted from what is actually loaded.
present=0
for m in mxc_epdc_v2_fb bcmdhd g_mass_storage cyttsp5_i2c; do loaded "$m" && present=$((present + 1)); done
touch_present=0; loaded cyttsp5_i2c && touch_present=1
usb_present=0; loaded g_mass_storage && usb_present=1
CLAIM="$present $touch_present $touch_present $usb_present $usb_present $rootfs_ro"
printf 'exec_claim=%s\n' "$CLAIM"

# Step 6: the execute module's dry-run. It arms the trampoline and stops before
# the caches; closing the device frees everything it staged.
stage insmod_exec
insmod "$HERE/$EXEC.ko" || { result=step_exec_insmod_refused; dmesg | grep -E "$EXEC|disagrees|version magic" | tail -20; exit 0; }
exec_loaded=1
DEV=/dev/koa2-handoff-exec
if [ ! -c "$DEV" ] && [ -r /sys/class/misc/koa2-handoff-exec/dev ]; then
    MAJMIN=$(cat /sys/class/misc/koa2-handoff-exec/dev)
    DEV=$DIR/koa2-handoff-exec.node
    mknod "$DEV" c "${MAJMIN%%:*}" "${MAJMIN##*:}" || { result=step_exec_node_failed; exit 0; }
fi
stage exec_dryrun
KOA2_EXEC_CLAIM=$CLAIM "$HERE/koa2-stage-client" --device "$DEV" "$HERE/zImage" /sys/firmware/fdt \
    "$HERE/initramfs.cpio.gz" --exec-dryrun > "$DIR/client.txt" 2>&1
client_rc=$?
cat "$DIR/client.txt"
printf 'client_exit=%s\n' "$client_rc"
[ "$DEV" = "$DIR/koa2-handoff-exec.node" ] && rm -f "$DEV"
field() { tr ' ' '\n' < "$DIR/client.txt" | awk -F= -v k="$1" '$1 == k {print $2; exit}'; }
if [ "$(field exec_stopped_before_caches)" != 1 ] || [ "$(field exec_execute_available)" != 0 ]; then
    result=step_exec_report_missing
elif [ "$(field exec_copylist)" != 0 ] || [ "$(field exec_params)" != 0 ]; then
    result=step_exec_not_armed
elif [ "$client_rc" = 0 ] && [ "$(field exec_quiesce)" = 0 ]; then
    result=exec_armed_pending_restore
elif [ "$client_rc" = 3 ]; then
    # Armed and stopped before the caches, but the quiesce verdict refused --
    # most likely / is not read-only. Reported as its own result.
    result=exec_not_quiesced_pending_restore
else
    result=step_exec_client_failed
fi
exit 0
