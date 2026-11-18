#!/bin/sh
# Read-only hardware inspection; writes report files ONLY under /mnt/us/koa2-boot-preflight.
# No modules, CPU hotplug, hardware settings, network, mounts or reboot commands.
set -eu
BASE=/mnt/us/koa2-boot-preflight
[ -r /proc/version ] && [ -d /mnt/us/documents ] || exit 1
mkdir -p "$BASE"
mkdir "$BASE/.running" 2>/dev/null || exit 1
trap 'rmdir "$BASE/.running" 2>/dev/null || true' 0 1 2 15
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir "$DIR"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
read_text() {
    if [ -r "$1" ] && [ ! -d "$1" ]; then
        printf '\n%s:\n' "$1"
        cat "$1" 2>/dev/null || true
        printf '\n'
    fi
}
{
    printf 'Readom KOA2 boot preflight v1 -- NOT a boot experiment\n'
    date -u
    uname -a
    for file in /etc/prettyversion.txt /proc/cmdline /proc/iomem /proc/partitions /proc/sys/kernel/tainted; do read_text "$file"; done
    printf '\n[cpu_topology]\n'
    for file in /sys/devices/system/cpu/online /sys/devices/system/cpu/possible /sys/devices/system/cpu/present /sys/devices/system/cpu/cpu1/online; do read_text "$file"; done
    printf '\n[boot_symbols]\n'
    if [ -r /proc/kallsyms ]; then
        awk '$3 ~ /^(cpu_down|cpu_up|stop_machine|kallsyms_lookup_name|machine_kexec|imx_cpu_die|imx_cpu_kill|imx_gpcv2_set_core1_pdn_pup_by_software)$/ {print}' /proc/kallsyms || true
    fi
    printf '\n[fdt]\n'
    if [ -r /sys/firmware/fdt ]; then
        if dd if=/sys/firmware/fdt of="$DIR/stock-runtime.dtb" bs=65536 count=16 2>"$DIR/fdt-copy.log"; then
            wc -c "$DIR/stock-runtime.dtb"
            if [ -x "$HERE/inspect-fdt-arm" ]; then
                if "$HERE/inspect-fdt-arm" "$DIR/stock-runtime.dtb"; then
                    printf 'dtb_structural_check=passed (not boot approval)\n'
                else
                    printf 'dtb_structural_check=failed_or_checker_unavailable\n'
                fi
            fi
        else printf 'fdt_copy=failed\n'; fi
    else printf 'runtime_fdt=unavailable\n'; fi
    printf '\n[chosen_and_reserved_nodes]\n'
    for file in /sys/firmware/devicetree/base/chosen/bootargs /sys/firmware/devicetree/base/model /sys/firmware/devicetree/base/compatible; do
        if [ -r "$file" ]; then printf '%s:\n' "$file"; tr '\000' '\n' < "$file"; fi
    done
    if [ -d /sys/firmware/devicetree/base/reserved-memory ]; then
        find /sys/firmware/devicetree/base/reserved-memory -type f -name reg -print
    fi
    printf '\n[log_availability]\n'
    if [ -r /proc/printkbuf ]; then
        if dd if=/proc/printkbuf of="$DIR/previous-kernel.log" bs=65536 count=16 2>"$DIR/printk-copy.log"; then
            wc -c "$DIR/previous-kernel.log"
        else printf 'printk_copy=failed\n'; fi
    else printf 'printkbuf=unavailable\n'; fi
    if [ -d /sys/fs/pstore ]; then ls -l /sys/fs/pstore; fi
    printf '\n[complete]\n'
} > "$DIR/report.txt.tmp" 2>&1
mv "$DIR/report.txt.tmp" "$DIR/report.txt"
printf '%s\n' "$RUN" > "$BASE/LAST_RUN.txt"
printf 'preflight-v1 complete\n' > "$DIR/complete.txt"
echo 'Read-only boot report saved. Reconnect USB when convenient.'
