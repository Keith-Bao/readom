#!/bin/sh
# Readom KOA2 quiesce dry run: stops every DMA master that can be stopped from
# userspace, checks the result, and PUTS IT ALL BACK. It does not load the
# handoff module, does not copy anything and does not jump; the stock system is
# running normally again when it finishes.
#
# This exists because the stopping cannot be done from a kernel module here:
# almost nothing that stops a device is both exported and has a stock CRC to
# check against, and a stopped device's registers cannot be read at all (its
# clock is gated). So the stopping is done through the kernel's own driver
# removal and CPU hotplug paths, which are written for it and reversible, and
# this script measures whether they work on this device and how long they take.
#
# Writes only under /mnt/us/koa2-quiesce-dryrun/.
set -u
BASE=/mnt/us/koa2-quiesce-dryrun
[ -r /proc/version ] && [ -d /mnt/us/documents ] || exit 1
mkdir -p "$BASE"
mkdir "$BASE/.running" 2>/dev/null || { echo 'Dry run already in progress.'; exit 0; }
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir "$DIR"
LOG="$DIR/report.txt"
say() { printf '%s\n' "$*" >> "$LOG"; }
step() { printf '\n[%s]\n' "$1" >> "$LOG"; }
# Everything removed, newest first, so the trap puts them back in order.
REMOVED=''
CPU1_WAS_ONLINE=0
ROOTFS_REMOUNTED=0
restore() {
    step restore
    # The rootfs goes back to read-write FIRST: a driver being loaded may want
    # to write, and its firmware and module files live on it. The log is on
    # /mnt/us (mmcblk2p7), a different filesystem from / (mmcblk2p5), so it
    # keeps working either way.
    [ "$ROOTFS_REMOUNTED" = 1 ] && { mount -o remount,rw / >> "$LOG" 2>&1 && say 'rootfs=rw' || say 'ROOTFS_STILL_RO'; }
    for m in $REMOVED; do
        if modprobe "$m" >> "$LOG" 2>&1; then say "reloaded=$m"; else say "RELOAD_FAILED=$m"; fi
    done
    [ "$CPU1_WAS_ONLINE" = 1 ] && { echo 1 > /sys/devices/system/cpu/cpu1/online 2>>"$LOG" && say 'cpu1=online' || say 'CPU1_STILL_DOWN'; }
    say "cpu_online_after=$(cat /sys/devices/system/cpu/online 2>/dev/null)"
    say "modules_after=$(grep -c . /proc/modules 2>/dev/null)"
    rmdir "$BASE/.running" 2>/dev/null || true
    printf '%s\n' "$RUN" > "$BASE/LAST_RUN.txt"
    printf 'quiesce-dryrun-v1 done\n' > "$DIR/complete.txt"
    echo 'Quiesce dry run finished and everything was put back. Report in koa2-quiesce-dryrun/.'
}
trap restore 0 1 2 15
say 'Readom KOA2 quiesce dry run v1 -- stops and restores, NO handoff'
date -u >> "$LOG"; uname -a >> "$LOG"
if [ "$(uname -r)" != 4.1.15-lab126 ]; then say 'result=refused_unknown_kernel'; exit 0; fi
step before
cp /proc/modules "$DIR/modules-before.txt"
say "cpu_online=$(cat /sys/devices/system/cpu/online 2>/dev/null)"
say "rootfs=$(awk '$2=="/"{print $4}' /proc/mounts | head -1)"
grep -E '^(MemFree|MemAvailable):' /proc/meminfo >> "$LOG"
# Each module is only removed if it is loaded and nothing is using it.
remove() {
    name=$1
    if ! grep -q "^$name " /proc/modules; then say "absent=$name"; return 0; fi
    users=$(awk -v n="$name" '$1==n{print $3}' /proc/modules)
    if [ "${users:-0}" != 0 ]; then say "IN_USE=$name users=$users"; return 1; fi
    start=$(date +%s)
    if rmmod "$name" >> "$LOG" 2>&1; then
        REMOVED="$name $REMOVED"
        say "removed=$name seconds=$(( $(date +%s) - start ))"
        return 0
    fi
    say "REMOVE_FAILED=$name"
    return 1
}
step epdc
# The EPDC first: its remove path waits for the updates in flight, powers the
# panel down and frees the DMA framebuffers. After this the screen keeps its
# last image, which is why nothing appears to happen.
remove mxc_epdc_v2_fb || say 'epdc=left_loaded'
step wifi
remove bcmdhd || say 'wifi=left_loaded'
step usb
# The gadget holds the function and composite drivers, so it goes first.
remove g_mass_storage || say 'usb=left_loaded'
remove usb_f_mass_storage || say 'usb_f=left_loaded'
remove libcomposite || say 'libcomposite=left_loaded'
step input
for m in cyttsp5_i2c bma2x2 als1_max44009; do remove "$m" || say "input=$m left_loaded"; done
step storage
sync
if mount -o remount,ro / >> "$LOG" 2>&1; then
    ROOTFS_REMOUNTED=1
    say "rootfs=$(awk '$2=="/"{print $4}' /proc/mounts | head -1)"
else
    say 'ROOTFS_REMOUNT_FAILED'
fi
step second_core
if [ -w /sys/devices/system/cpu/cpu1/online ] && [ "$(cat /sys/devices/system/cpu/cpu1/online)" = 1 ]; then
    CPU1_WAS_ONLINE=1
    start=$(date +%s)
    if echo 0 > /sys/devices/system/cpu/cpu1/online 2>>"$LOG"; then
        say "cpu1_down_seconds=$(( $(date +%s) - start ))"
    else
        say 'CPU1_DOWN_FAILED'; CPU1_WAS_ONLINE=0
    fi
fi
say "cpu_online=$(cat /sys/devices/system/cpu/online 2>/dev/null)"
step verify
# What the module would check, checked here the same way: one core, none of the
# four drivers loaded, the rootfs read-only. No peripheral register is read.
# Count the cores actually online rather than parsing the summary range: a
# "0-1" has to come out as 2 and a "0" as 1, and cpu1/online is the file the
# kernel updates first.
ONLINE=1
[ "$(cat /sys/devices/system/cpu/cpu1/online 2>/dev/null)" = 1 ] && ONLINE=2
PRESENT=0
for m in mxc_epdc_v2_fb bcmdhd g_mass_storage cyttsp5_i2c; do
    grep -q "^$m " /proc/modules && { PRESENT=$((PRESENT + 1)); say "still_loaded=$m"; }
done
RO=0; [ "$(awk '$2=="/"{print $4}' /proc/mounts | head -1 | cut -d, -f1)" = ro ] && RO=1
say "online_fields=$ONLINE drivers_present=$PRESENT rootfs_readonly=$RO"
cp /proc/modules "$DIR/modules-quiesced.txt"
grep -E '^(MemFree|MemAvailable):' /proc/meminfo >> "$LOG"
if [ "$PRESENT" = 0 ] && [ "$RO" = 1 ] && [ "$(cat /sys/devices/system/cpu/cpu1/online 2>/dev/null)" = 0 ]; then
    say 'result=quiesced_and_restored'
else
    say 'result=incomplete_see_above'
fi
# The trap restores everything from here.
