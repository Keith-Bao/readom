#!/bin/sh
# Readom KOA2 marker test, in two halves because the thing being tested is
# whether a marker survives a reset.
#
#   place  loads the staging module, stages and seals, asks it to write one
#          marker into the live half of the Lab126 printk buffer, unloads.
#   check  run AFTER a restart: reads /proc/printkbuf, which publishes the
#          PREVIOUS boot's buffer, and looks for the marker in it.
#
# The marker cannot be read back in the same boot: the live half is not what
# /proc/printkbuf publishes. That is the whole point -- a marker is only worth
# writing if it is still there after the device has reset.
#
# No jump, no payload copy, no partition write, no core taken down. What it
# costs is the last 256 bytes of one boot's kernel log. See
# ports/koa2/recovery.md section 4.
#
# Writes only under /mnt/us/koa2-marker-test/.
set -u
BASE=/mnt/us/koa2-marker-test
[ -r /proc/version ] && [ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WHAT=${1:-place}
mkdir -p "$BASE"
mkdir "$BASE/.running" 2>/dev/null || { echo 'Marker test already running.'; exit 0; }
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
    echo "Marker test ($WHAT): $1. Report in koa2-marker-test/."
    exit 0
}
say "Readom KOA2 marker test v1 -- $WHAT; writes a marker, NO jump"
date -u >> "$LOG"; uname -a >> "$LOG"
step identity
REL=$(uname -r)
say "release=$REL"
[ "$REL" = 4.1.15-lab126 ] || finish refused_unknown_kernel

if [ "$WHAT" = check ]; then
    # /proc/printkbuf is the previous boot's buffer. A marker in it means the
    # marker was written, reached DRAM, and survived the restart.
    step read_previous_boot
    [ -r /proc/printkbuf ] || finish refused_no_printkbuf
    dd if=/proc/printkbuf of="$DIR/previous-boot.bin" bs=4096 2>> "$LOG"
    say "bytes=$(wc -c < "$DIR/previous-boot.bin")"
    # The text comes out with tr, not strings. The boot preflight already read
    # this buffer on this device that way, while nothing says this busybox has a
    # strings applet at all -- and a missing applet produces an empty result,
    # which is precisely what a marker that did not survive looks like.
    tr '\000' '\n' < "$DIR/previous-boot.bin" > "$DIR/previous-boot.txt" 2>> "$LOG"
    # The read has to be shown to have worked before its silence means anything.
    # Every boot of this kernel leaves hundreds of bracketed timestamps in here,
    # so finding none of them says the buffer was not read, not that the marker
    # is gone. grep -c prints 0 and exits 1 when it matches nothing, so the
    # count is taken on its own and the exit status left alone.
    LINES=$(grep -c '\[ *[0-9][0-9]*\.[0-9]' "$DIR/previous-boot.txt" 2>/dev/null)
    [ -n "$LINES" ] || LINES=0
    say "previous_boot_log_lines=$LINES"
    # Only the marker lines are kept: the rest of that log is the device s own
    # and is not ours to upload.
    grep KOA2_HANDOFF "$DIR/previous-boot.txt" > "$DIR/markers.txt" 2>/dev/null || true
    say "markers_found=$(wc -l < "$DIR/markers.txt")"
    cat "$DIR/markers.txt" >> "$LOG"
    rm -f "$DIR/previous-boot.bin" "$DIR/previous-boot.txt"
    if [ -s "$DIR/markers.txt" ]; then finish marker_survived; fi
    [ "$LINES" -ge 20 ] || finish refused_buffer_unreadable
    finish marker_absent
fi

[ "$WHAT" = place ] || finish refused_unknown_argument
step kit
( cd "$HERE" && sha256sum -c SHA256SUMS ) >> "$LOG" 2>&1 || finish refused_kit_damaged
if grep -q '^koa2_handoff_stage ' /proc/modules 2>/dev/null; then finish refused_module_already_loaded; fi
step insmod
insmod "$HERE/koa2_handoff_stage.ko" >> "$LOG" 2>&1
RC=$?
say "insmod_exit=$RC"
if [ "$RC" -ne 0 ]; then
    dmesg | grep -E 'koa2_handoff_stage|disagrees|no symbol version|version magic' | tail -40 >> "$LOG"
    finish insmod_refused
fi
step device
DEV=/dev/koa2-handoff-stage
if [ ! -c "$DEV" ] && [ -r /sys/class/misc/koa2-handoff-stage/dev ]; then
    MAJMIN=$(cat /sys/class/misc/koa2-handoff-stage/dev)
    DEV=$DIR/koa2-handoff-stage.node
    mknod "$DEV" c "${MAJMIN%%:*}" "${MAJMIN##*:}" && say "node=made_in_report_dir"
fi
step mark
"$HERE/koa2-stage-client" --device "$DEV" "$HERE/zImage" /sys/firmware/fdt \
    "$HERE/initramfs.cpio.gz" --mark >> "$LOG" 2>&1
CRC=$?
say "client_exit=$CRC"
step rmmod
rmmod koa2_handoff_stage >> "$LOG" 2>&1
say "rmmod_exit=$?"
[ "$DEV" = "$DIR/koa2-handoff-stage.node" ] && rm -f "$DEV"
step after
dmesg | grep -i koa2 | tail -20 >> "$LOG" 2>&1
if [ "$CRC" -ne 0 ]; then finish mark_failed; fi
say 'Now restart the Kindle and run "Marker test: check after restart".'
finish marker_placed
