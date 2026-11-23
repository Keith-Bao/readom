#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Observe normal stock sleep and wake. The only writes are test reports.
set -u
BASE=/mnt/us/koa2-quiesce-dryrun
PENDING="$BASE/STOCK_SLEEP_PENDING.txt"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1

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

snapshot() {
    for event in /sys/class/input/event*; do
        [ "$(cat "$event/device/name" 2>/dev/null)" = cyttsp5_mt ] || continue
        node="/dev/input/${event##*/}"
        identity=$(stat -L -c '%d:%i' "$node") || return 1
        reader=$(xorg_reader "$node") || return 1
        boot=$(cat /proc/sys/kernel/random/boot_id) || return 1
        [ -n "$boot" ] || return 1
        total=$("$HERE/touch-events" --suspend-total-ms) || return 1
        case "$total" in ''|*[!0-9]*) return 1 ;; esac
        printf '%s_boot=%s\n%s_node=%s\n%s_inode=%s\n%s_reader=%s\n%s_suspend_total_ms=%s\n' \
            "$1" "$boot" "$1" "$node" "$1" "$identity" "$1" "$reader" "$1" "$total"
        return
    done
    return 1
}

field() {
    awk -v key="$1=" 'index($0, key) == 1 {print substr($0, length(key) + 1); exit}' "$LOG"
}

power_evidence() {
    stats=/sys/kernel/debug/suspend_stats
    if [ -r "$stats" ]; then
        awk -v phase="$1" '$1 ~ /^(success|fail):$/ && $2 ~ /^[0-9]+$/ {
            sub(/:$/, "", $1); print phase "_pm_" $1 "=" $2
        }' "$stats"
    else
        printf '%s_pm_stats=unavailable\n' "$1"
    fi
    dmesg 2>/dev/null | awk -v phase="$1" '
        /cyttsp5.*fb_notifier_callback: POWERDOWN!/ {++blank}
        /cyttsp5.*fb_notifier_callback: UNBLANK!/ {++unblank}
        /PM: Entering .* sleep/ {++entry}
        /PM: Finishing wakeup/ {++wake}
        /PM: (late suspend of devices failed|noirq suspend of devices failed|Some devices failed to suspend)/ {++fail}
        END {
            if (!NR) { print phase "_pm_log=unavailable"; exit }
            print phase "_fb_blank=" blank + 0
            print phase "_fb_unblank=" unblank + 0
            print phase "_pm_entry=" entry + 0
            print phase "_pm_wake=" wake + 0
            print phase "_pm_errors=" fail + 0
        }'
}

finish() {
    printf 'result=%s\n' "$1" >> "$LOG"
    printf 'Stock sleep check: %s\n' "$1"
}

[ "$(uname -r)" = 4.1.15-lab126 ] && [ -d /mnt/us/documents ] || exit 1
checksums=$(awk '$2 == "stock-sleep.sh" || $2 == "touch-events" {print; ++seen[$2]}
    END {if (seen["stock-sleep.sh"] != 1 || seen["touch-events"] != 1) exit 1}' "$HERE/SHA256SUMS") || exit 1
(cd "$HERE" && printf '%s\n' "$checksums" | sha256sum -c - >/dev/null) || exit 1
mkdir -p "$BASE" && mkdir "$BASE/.sleep-running" || exit 1
trap 'rmdir "$BASE/.sleep-running"' 0
trap 'exit 1' HUP INT TERM

case "${1:-}" in
    before)
        if [ -e "$PENDING" ]; then
            printf '%s\n' 'A stock sleep observation is already pending. Run the after-wake check first.'
            exit 1
        fi
        RUN=$(date -u +%Y%m%d-%H%M%S)-$$
        mkdir "$BASE/$RUN" || exit 1
        LOG="$BASE/$RUN/report.txt"
        printf '%s\n' 'Readom KOA2 stock sleep observation v2' > "$LOG" || exit 1
        if ! snapshot before >> "$LOG" 2>&1; then
            finish stock_sleep_baseline_unavailable
            exit 1
        fi
        power_evidence before >> "$LOG"
        printf '%s\n' "$RUN" > "$PENDING" || exit 1
        ;;
    after)
        RUN=$(cat "$PENDING") || exit 1
        printf '%s\n' "$RUN" | grep -Eq '^[0-9]{8}-[0-9]{6}-[0-9]+$' || exit 1
        LOG="$BASE/$RUN/report.txt"
        [ -f "$LOG" ] || exit 1
        power_evidence after >> "$LOG"
        if ! snapshot after >> "$LOG" 2>&1; then
            finish stock_sleep_input_unavailable
        elif [ "$boot" != "$(field before_boot)" ]; then
            finish stock_sleep_boot_changed
        else
            delta=$(awk -v before="$(field before_suspend_total_ms)" -v after="$total" \
                'BEGIN {if (before ~ /^[0-9]+$/) printf "%.0f\n", after - before}')
            printf 'suspend_delta_ms=%s\n' "$delta" >> "$LOG"
            contact=unconfirmed
            if "$HERE/touch-events" "$node" >> "$LOG" 2>&1; then
                contact=observed
            fi
            printf 'after_touch=%s\n' "$contact" >> "$LOG"
            if ! awk -v delta="$delta" 'BEGIN {exit !(delta >= 1000)}'; then
                finish stock_sleep_not_observed
            elif [ "$node" != "$(field before_node)" ] || [ "$identity" != "$(field before_inode)" ] ||
                [ "$reader" != "$(field before_reader)" ]; then
                finish stock_sleep_input_changed
            elif [ "$contact" = observed ]; then
                finish stock_sleep_touch_observed
            else
                finish stock_sleep_touch_unconfirmed
            fi
        fi
        rm -f "$PENDING"
        ;;
    *) exit 2 ;;
esac
