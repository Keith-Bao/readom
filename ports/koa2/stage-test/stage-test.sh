#!/bin/sh
# Readom KOA2 stage test: loads koa2_handoff_stage.ko into the stock kernel,
# stages zImage + the runtime FDT + initramfs, seals, records the result,
# unloads the module. The module has no jump: nothing here can leave the stock
# system, write a partition, stop a core or reboot. Writes only under
# /mnt/us/koa2-stage-test/. Also copies the stock .ko files (read only) so the
# CRCs can be checked on the computer whatever the load does.
set -u
BASE=/mnt/us/koa2-stage-test
[ -r /proc/version ] && [ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$BASE"
mkdir "$BASE/.running" 2>/dev/null || { echo 'Stage test already running.'; exit 0; }
trap 'rmdir "$BASE/.running" 2>/dev/null || true' 0 1 2 15
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir "$DIR"
LOG="$DIR/report.txt"
say() { printf '%s\n' "$*" >> "$LOG"; }
step() { printf '\n[%s]\n' "$1" >> "$LOG"; }
finish() {
    step complete
    say "result=$1"
    printf '%s\n' "$RUN" > "$BASE/LAST_RUN.txt"
    printf 'stage-test-v1 %s\n' "$1" > "$DIR/complete.txt"
    echo "Stage test: $1. Report in koa2-stage-test/."
    exit 0
}
say 'Readom KOA2 stage test v1 -- stages and checks only, NO jump'
date -u >> "$LOG"; uname -a >> "$LOG"
step identity
REL=$(uname -r); VER=$(uname -v)
say "release=$REL"
say "version=$VER"
if [ "$REL" != 4.1.15-lab126 ] || [ "$VER" != '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ]; then
    finish refused_unknown_kernel
fi
step kit
( cd "$HERE" && sha256sum -c SHA256SUMS ) >> "$LOG" 2>&1 || finish refused_kit_damaged
step stock_modules
MODDIR=/lib/modules/$REL
if [ -d "$MODDIR" ]; then
    mkdir "$DIR/stock-modules"
    ( cd "$MODDIR" && find . -name '*.ko' -type f ) | while read -r f; do
        mkdir -p "$DIR/stock-modules/$(dirname "$f")"
        cp "$MODDIR/$f" "$DIR/stock-modules/$f" || say "copy_failed=$f"
    done
    say "modules_copied=$(find "$DIR/stock-modules" -name '*.ko' | wc -l)"
else say 'module_dir=unavailable'; fi
[ -r /proc/modules ] && cat /proc/modules > "$DIR/proc-modules-before.txt"
step before
say "tainted=$(cat /proc/sys/kernel/tainted 2>/dev/null)"
# Recorded, not changed: 1 means an oops in the module panics the kernel and,
# after CONFIG_PANIC_TIMEOUT=5 s, restarts it through the stock restart path.
say "panic_on_oops=$(cat /proc/sys/kernel/panic_on_oops 2>/dev/null)"
say "panic=$(cat /proc/sys/kernel/panic 2>/dev/null)"
grep -E '^(MemFree|MemAvailable|Cached):' /proc/meminfo >> "$LOG" 2>/dev/null
dmesg > "$DIR/dmesg-before.txt" 2>&1
if grep -q '^koa2_handoff_stage ' /proc/modules 2>/dev/null; then finish refused_module_already_loaded; fi
step insmod
insmod "$HERE/koa2_handoff_stage.ko" >> "$LOG" 2>&1
RC=$?
say "insmod_exit=$RC"
dmesg > "$DIR/dmesg-after-insmod.txt" 2>&1
if [ "$RC" -ne 0 ]; then
    grep -E 'koa2_handoff_stage|disagrees|no symbol version|version magic' "$DIR/dmesg-after-insmod.txt" | tail -40 >> "$LOG"
    finish insmod_refused
fi
step device
DEV=/dev/koa2-handoff-stage
if [ ! -c "$DEV" ] && [ -r /sys/class/misc/koa2-handoff-stage/dev ]; then
    MAJMIN=$(cat /sys/class/misc/koa2-handoff-stage/dev)
    DEV=$DIR/koa2-handoff-stage.node
    mknod "$DEV" c "${MAJMIN%%:*}" "${MAJMIN##*:}" && say "node=made_in_report_dir"
fi
ls -l "$DEV" >> "$LOG" 2>&1
step stage
"$HERE/koa2-stage-client" --device "$DEV" "$HERE/zImage" /sys/firmware/fdt "$HERE/initramfs.cpio.gz" >> "$LOG" 2>&1
CRC=$?
say "client_exit=$CRC"
grep -E '^(MemFree|MemAvailable):' /proc/meminfo >> "$LOG" 2>/dev/null
step rmmod
rmmod koa2_handoff_stage >> "$LOG" 2>&1
say "rmmod_exit=$?"
[ "$DEV" = "$DIR/koa2-handoff-stage.node" ] && rm -f "$DEV"
step after
say "tainted=$(cat /proc/sys/kernel/tainted 2>/dev/null)"
dmesg > "$DIR/dmesg-after.txt" 2>&1
if [ "$CRC" -eq 0 ]; then finish sealed_plan_valid; else finish seal_failed; fi
