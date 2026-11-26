#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Touch only: put the stock touch controller to sleep through its own driver,
# show independently that the I2C bus went quiet, then wake it and check that
# Xorg still reads the same input node. The driver stays loaded throughout.
# The only writes are drv_debug (4 = the stock cyttsp5_core_sleep, 5 = the
# stock wake) and this test's report. ports/koa2/handoff-quiesce.md.
set -u
BASE=/mnt/us/koa2-touch-quiesce
TOUCH=/sys/bus/i2c/devices/1-0024
I2C_IRQ=30a30000.i2c
TOUCH_IRQ=1-0024
# Longer than the 2 s CY_WATCHDOG_TIMEOUT, so a running watchdog shows up.
WINDOW=5
[ -d /mnt/us/documents ] || exit 1
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 touch-only sleep and wake v1'
date -u

finish() {
    printf 'result=%s\n' "$1"
    exit 0
}

# irq number and per-CPU "CPUk=count" pairs for the line named $1.
# Matching by header CPU name handles dynamic CPU1 hotplug offline/online,
# which adds or removes columns in /proc/interrupts without resetting counters.
irq_line() {
    awk -v name="$1" 'NR == 1 {
        ncpus = NF
        for (i = 1; i <= NF; ++i) cpu[i] = $i
        next
    }
    $NF == name {
        irq = $1; sub(/:$/, "", irq)
        out = irq
        for (i = 1; i <= ncpus && (i + 1) <= NF && $(i + 1) ~ /^[0-9]+$/; ++i)
            out = out " " cpu[i] "=" $(i + 1)
        print out; found = 1; exit
    } END { exit !found }' /proc/interrupts
}

# Total new interrupts between two irq_line snapshots ($1=before, $2=after).
# A CPU column that disappears (hotplugged offline) contributes 0; a CPU column
# that appears or stays online contributes its non-negative delta.
irq_delta() {
    awk -v before="$1" -v after="$2" 'BEGIN {
        nb = split(before, b, " ")
        for (i = 2; i <= nb; ++i) { split(b[i], kv, "="); prev[kv[1]] = kv[2] + 0 }
        na = split(after, a, " ")
        total = 0
        for (i = 2; i <= na; ++i) {
            split(a[i], kv, "=")
            cur = kv[2] + 0
            if (kv[1] in prev) {
                d = cur - prev[kv[1]]
                if (d > 0) total += d
            } else {
                total += cur
            }
        }
        print total
    }'
}

# State letter of the threaded handler for the touch line.
irq_thread_state() {
    for process in /proc/[0-9]*; do
        [ "$(cat "$process/comm" 2>/dev/null)" = "irq/$TOUCH_IRQ_NUMBER-$TOUCH_IRQ" ] || continue
        awk '{sub(/^.*\) /, ""); print $1; exit}' "$process/stat" 2>/dev/null
        return
    done
    return 1
}

regulator_state() {
    for regulator in /sys/class/regulator/*; do
        [ "$(cat "$regulator/name" 2>/dev/null)" = MAX77796-LSW1 ] || continue
        cat "$regulator/state"
        return
    done
    return 1
}

# Level of the touch_1v8 supply GPIO, as the stock gpiolib reports it.
supply_gpio() {
    awk 'index($0, "(touch_1v8 ") || index($0, "(touch_1v8)") {
        sub(/^[^)]*\) */, ""); print $1 "_" $2; found = 1; exit
    } END { exit !found }' /sys/kernel/debug/gpio
}

sleep_status() {
    cat "$TOUCH/sleep_status"
}

touch_node() {
    for event in /sys/class/input/event*; do
        [ "$(cat "$event/device/name" 2>/dev/null)" = cyttsp5_mt ] || continue
        printf '/dev/input/%s\n' "${event##*/}"
        return
    done
    return 1
}

xorg_reader() {
    for process in /proc/[0-9]*; do
        [ "$(cat "$process/comm" 2>/dev/null)" = Xorg ] || continue
        start=$(awk 'sub(/^.*\) /, "") && $20 ~ /^[0-9]+$/ {print $20}' "$process/stat" 2>/dev/null)
        [ -n "$start" ] || continue
        for fd in "$process"/fd/*; do
            if [ "$fd" -ef "$1" ]; then
                printf '%s:%s:%s\n' "${process##*/}" "$start" "${fd##*/}"
                return
            fi
        done
    done
    return 1
}

# Anything else that could drive the bus or the driver while it sleeps.
other_users() {
    for process in /proc/[0-9]*; do
        for fd in "$process"/fd/*; do
            for target in /dev/i2c-1 /proc/touch; do
                [ -e "$target" ] && [ "$fd" -ef "$target" ] || continue
                printf 'other_user=%s comm=%s target=%s\n' "${process##*/}" \
                    "$(tr -cd '[:alnum:]_. -' < "$process/comm" 2>/dev/null)" "$target"
            done
        done
    done
}

suspend_total() {
    value=$("$HERE/touch-events" --suspend-total-ms) || return 1
    case "$value" in ''|*[!0-9]*) return 1 ;; esac
    printf '%s\n' "$value"
}

log_count() {
    dmesg 2>/dev/null | awk -v text="$1" 'index($0, "drv_debug_store: " text) {++n} END {print n + 0}'
}

# Screen blank and unblank reach the driver through its framebuffer notifier
# and runtime PM; unblank can wake the controller in the middle of the window.
blank_count() {
    dmesg 2>/dev/null | awk 'index($0, "fb_notifier_callback: POWERDOWN!") || index($0, "fb_notifier_callback: UNBLANK!") {++n} END {print n + 0}'
}

# Counts of both interrupt lines over WINDOW seconds, as "i2c touch".
window_counts() {
    i2c_start=$(irq_line "$I2C_IRQ") && touch_start=$(irq_line "$TOUCH_IRQ") || return 1
    sleep "$WINDOW"
    i2c_end=$(irq_line "$I2C_IRQ") && touch_end=$(irq_line "$TOUCH_IRQ") || return 1
    printf '%s %s\n' "$(irq_delta "$i2c_start" "$i2c_end")" "$(irq_delta "$touch_start" "$touch_end")"
}

# On i.MX7D, gpio-mxc reads GPIO_PSR for output pins (bgpio_init without
# BGPIOF_READ_OUTPUT_REG_SET), and touch_1v8 (GPIO1_IO12) has SION=0, so
# /sys/kernel/debug/gpio always reports out_lo even when driven high.
# Record supply_gpio for diagnostics, but judge awake power by sleep_status
# plus MAX77796-LSW1 regulator state (and the awake I2C positive control).
powered() {
    [ "$(sleep_status)" = on ] && [ "$(regulator_state)" = enabled ]
}

release=$(uname -r)
version=$(uname -v)
printf 'release=%s\nversion=%s\n' "$release" "$version"
[ "$release" = 4.1.15-lab126 ] && [ "$version" = '#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023' ] ||
    finish refused_unknown_kernel
checksums=$(awk '$2 == "touch-quiesce.sh" || $2 == "touch-events" {print; ++seen[$2]}
    END {if (seen["touch-quiesce.sh"] != 1 || seen["touch-events"] != 1) exit 1}' "$HERE/SHA256SUMS") ||
    finish refused_kit_unverified
(cd "$HERE" && printf '%s\n' "$checksums" | sha256sum -c - >/dev/null) || finish refused_kit_unverified
mkdir "$BASE/.running" 2>/dev/null || finish refused_test_already_running
trap 'rmdir "$BASE/.running"' 0

# Preconditions: the stock driver is bound and awake, every piece of evidence
# this test relies on can be read, and nothing but Xorg uses the touch path.
[ -e "$TOUCH/driver" ] && [ -w "$TOUCH/drv_debug" ] || finish refused_touch_driver_missing
line=$(irq_line "$TOUCH_IRQ") || finish refused_touch_irq_missing
set -- $line
TOUCH_IRQ_NUMBER=$1
irq_line "$I2C_IRQ" >/dev/null || finish refused_i2c_irq_missing
thread=$(irq_thread_state) || finish refused_irq_thread_missing
regulator=$(regulator_state) || finish refused_regulator_missing
gpio=$(supply_gpio) || finish refused_supply_gpio_missing
status=$(sleep_status) || finish refused_sleep_status_missing
node=$(touch_node) || finish refused_touch_node_missing
identity=$(stat -L -c '%d:%i' "$node") || finish refused_touch_node_missing
reader=$(xorg_reader "$node") || finish refused_xorg_reader_missing
total_before=$(suspend_total) || finish refused_suspend_clock_unavailable
blanks_before=$(blank_count)
printf 'touch_irq=%s\nirq_thread=%s\nregulator=%s\nsupply_gpio=%s\nsleep_status=%s\n' \
    "$TOUCH_IRQ_NUMBER" "$thread" "$regulator" "$gpio" "$status"
printf 'node=%s\ninode=%s\nreader=%s\n' "$node" "$identity" "$reader"
[ "$status" = on ] && [ "$regulator" = enabled ] || finish refused_touch_not_awake
users=$(other_users)
if [ -n "$users" ]; then
    printf '%s\n' "$users"
    finish refused_other_touch_users
fi

# Positive control: awake, the 2 s watchdog talks to the controller, so the
# bus counter must move in the window. If it does not, a quiet window later
# would prove nothing.
counts=$(window_counts) || finish refused_irq_counts_unreadable
set -- $counts
printf 'awake_i2c_irqs=%s\nawake_touch_irqs=%s\n' "$1" "$2"
[ "$1" -gt 0 ] || finish refused_no_awake_bus_activity

# From here on the controller may be asleep. Whatever happens, one wake
# attempt runs before the lock is released.
slept=0
result=touch_interrupted
wake_and_report() {
    trap '' HUP INT TERM
    wake=ok
    if [ "$slept" = 1 ]; then
        resumed=$(log_count 'Resume succeeded') resume_failed=$(log_count 'Resume failed')
        echo 5 > "$TOUCH/drv_debug" || wake=write_failed
        sleep 1
        printf 'resume_logged=%s\nresume_failed_logged=%s\n' \
            $(( $(log_count 'Resume succeeded') - resumed )) $(( $(log_count 'Resume failed') - resume_failed ))
        [ $(( $(log_count 'Resume failed') - resume_failed )) -eq 0 ] || wake=failed_logged
        powered || wake=not_powered
    fi
    printf 'wake=%s\n' "$wake"
    contact=unconfirmed
    if [ "$wake" = ok ]; then
        "$HERE/touch-events" "$node" && contact=observed
        counts=$(window_counts) || counts='0 0'
        set -- $counts
        printf 'woken_i2c_irqs=%s\nafter_touch=%s\n' "$1" "$contact"
        after_node=$(touch_node) || after_node=
        after_identity=$(stat -L -c '%d:%i' "$after_node" 2>/dev/null) || after_identity=
        after_reader=$(xorg_reader "$after_node") || after_reader=
    fi
    if [ "$wake" != ok ]; then
        result=touch_wake_failed_restart_required
    elif [ "$contact" != observed ] && [ "$1" -le 0 ]; then
        result=touch_wake_failed_restart_required
    elif [ "$after_node" != "$node" ] || [ "$after_identity" != "$identity" ] ||
        [ "$after_reader" != "$reader" ]; then
        result=touch_input_changed
    elif [ "$result" = touch_quiet_pending_wake ]; then
        result=touch_quiesce_restored_touch_$contact
    fi
    rmdir "$BASE/.running"
    printf 'result=%s\n' "$result"
}
trap 'wake_and_report' 0
trap 'exit 1' HUP INT TERM

failed=$(log_count 'Suspend failed') succeeded=$(log_count 'Suspend succeeded')
slept=1
echo 4 > "$TOUCH/drv_debug" || { result=touch_sleep_write_failed; exit 0; }
# Let a handler that was already running finish before the window starts.
sleep 1
logged=$(( $(log_count 'Suspend succeeded') - succeeded ))
sleep_failed=$(( $(log_count 'Suspend failed') - failed ))
printf 'suspend_logged=%s\nsuspend_failed_logged=%s\n' "$logged" "$sleep_failed"
status=$(sleep_status) regulator=$(regulator_state) gpio=$(supply_gpio) thread=$(irq_thread_state)
printf 'asleep_status=%s\nasleep_regulator=%s\nasleep_supply_gpio=%s\nasleep_irq_thread=%s\n' \
    "$status" "$regulator" "$gpio" "$thread"
counts=$(window_counts) || { result=touch_irq_counts_unreadable; exit 0; }
set -- $counts
printf 'asleep_i2c_irqs=%s\nasleep_touch_irqs=%s\n' "$1" "$2"
total_after=$(suspend_total) || { result=touch_suspend_clock_unavailable; exit 0; }
printf 'system_suspend_ms=%s\n' $((total_after - total_before))
printf 'screen_changes=%s\n' $(( $(blank_count) - blanks_before ))
# sleep_status reads off even when the power-down failed, so it is recorded
# but never trusted alone: the regulator, the supply GPIO and the bus decide.
if [ "$sleep_failed" -ne 0 ] || [ "$logged" -ne 1 ]; then
    result=touch_sleep_not_confirmed_by_driver
elif [ $((total_after - total_before)) -ge 1000 ] || [ "$(blank_count)" != "$blanks_before" ]; then
    # A system suspend or a screen change in between runs the stock PM paths.
    result=touch_test_disturbed_by_system_sleep
elif [ "$status" != off ] || [ "$regulator" != disabled ] || [ "$gpio" != out_lo ]; then
    result=touch_not_powered_down
elif [ "$thread" != S ]; then
    result=touch_irq_thread_busy
elif [ "$1" -ne 0 ] || [ "$2" -ne 0 ]; then
    result=touch_bus_not_quiet
else
    result=touch_quiet_pending_wake
fi
exit 0
