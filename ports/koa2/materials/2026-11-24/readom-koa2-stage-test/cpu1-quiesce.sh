#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# CPU1 only: take secondary core offline via stock sysfs hotplug, verify it
# stays offline and takes no interrupts during a short window, then bring it
# back online. Never loads the handoff module, never touches storage/drivers.
set -u
BASE=/mnt/us/koa2-cpu1-quiesce
CPU_ONLINE=/sys/devices/system/cpu/online
CPU1_ONLINE=/sys/devices/system/cpu/cpu1/online
WINDOW=${KOA2_CPU1_WINDOW:-2}
[ -d /mnt/us/documents ] || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
exec > "$DIR/report.txt" 2>&1
printf '%s\n' 'Readom KOA2 CPU1-only offline and online v1'
date -u

finish() {
    printf 'result=%s\n' "$1"
    printf '%s\n' "$RUN" > "$BASE/LAST_RUN.txt"
    exit 0
}

cpu1_irq_total() {
    awk '
        NR == 1 {
            cpu1_col = 0
            for (i = 1; i <= NF; ++i) {
                if ($i == "CPU1") { cpu1_col = i + 1; break }
            }
            next
        }
        cpu1_col > 0 && $1 ~ /^[0-9A-Za-z]+:$/ && $(cpu1_col) ~ /^[0-9]+$/ {
            total += $(cpu1_col)
        }
        END {
            if (cpu1_col == 0) exit 1
            print total + 0
        }
    ' /proc/interrupts 2>/dev/null
}

restore_cpu1() {
    if [ -w "$CPU1_ONLINE" ]; then
        printf '1\n' > "$CPU1_ONLINE" 2>/dev/null || true
    fi
}

if [ "$(uname -r)" != 4.1.15-lab126 ]; then
    finish refused_unknown_kernel
fi
[ -r "$CPU_ONLINE" ] && [ -r "$CPU1_ONLINE" ] && [ -w "$CPU1_ONLINE" ] || finish refused_cpu1_hotplug_unavailable

online_before=$(cat "$CPU_ONLINE" 2>/dev/null)
cpu1_before=$(cat "$CPU1_ONLINE" 2>/dev/null)
printf 'online_before=%s\n' "$online_before"
printf 'cpu1_before=%s\n' "$cpu1_before"
if [ "$online_before" != "0-1" ] || [ "$cpu1_before" != "1" ]; then
    finish refused_unexpected_cpu_state
fi

trap 'restore_cpu1' EXIT HUP INT TERM

if ! printf '0\n' > "$CPU1_ONLINE" 2>/dev/null; then
    restore_cpu1
    trap - EXIT HUP INT TERM
    finish cpu1_offline_write_failed
fi

online_off_start=$(cat "$CPU_ONLINE" 2>/dev/null)
cpu1_off_start=$(cat "$CPU1_ONLINE" 2>/dev/null)
printf 'online_offline_start=%s\n' "$online_off_start"
printf 'cpu1_offline_start=%s\n' "$cpu1_off_start"
if [ "$online_off_start" != "0" ] || [ "$cpu1_off_start" != "0" ]; then
    restore_cpu1
    trap - EXIT HUP INT TERM
    finish cpu1_did_not_go_offline
fi

irqs_off_start=$(cpu1_irq_total || printf 'unavailable')
sleep "$WINDOW"
online_off_end=$(cat "$CPU_ONLINE" 2>/dev/null)
cpu1_off_end=$(cat "$CPU1_ONLINE" 2>/dev/null)
irqs_off_end=$(cpu1_irq_total || printf 'unavailable')
printf 'online_offline_end=%s\n' "$online_off_end"
printf 'cpu1_offline_end=%s\n' "$cpu1_off_end"
printf 'cpu1_irqs_offline_start=%s\n' "$irqs_off_start"
printf 'cpu1_irqs_offline_end=%s\n' "$irqs_off_end"

restore_ok=0
if printf '1\n' > "$CPU1_ONLINE" 2>/dev/null; then
    restore_ok=1
fi
trap - EXIT HUP INT TERM

online_after=$(cat "$CPU_ONLINE" 2>/dev/null)
cpu1_after=$(cat "$CPU1_ONLINE" 2>/dev/null)
printf 'online_after=%s\n' "$online_after"
printf 'cpu1_after=%s\n' "$cpu1_after"

printf '\n[dmesg_hotplug]\n'
dmesg 2>/dev/null | grep -E 'CPU1|cpu1|hotplug|gpc|imx_cpu' | tail -n 20 || true

if [ "$restore_ok" != "1" ] || [ "$online_after" != "0-1" ] || [ "$cpu1_after" != "1" ]; then
    finish cpu1_restore_failed_restart_required
fi
if [ "$online_off_end" != "0" ] || [ "$cpu1_off_end" != "0" ]; then
    finish cpu1_came_back_early
fi
if [ "$irqs_off_start" != "unavailable" ] && [ "$irqs_off_end" != "unavailable" ] && [ "$irqs_off_end" != "$irqs_off_start" ]; then
    finish cpu1_irqs_moved_while_offline
fi
finish cpu1_quiesce_restored
