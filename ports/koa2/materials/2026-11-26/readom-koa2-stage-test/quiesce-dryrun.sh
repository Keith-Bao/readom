#!/bin/sh
# Inspect the stock system without unloading drivers or changing mount flags.
set -u
BASE=/mnt/us/koa2-quiesce-dryrun
[ -r /proc/version ] && [ -d /mnt/us/documents ] || exit 1
RUN=$(date -u +%Y%m%d-%H%M%S)-$$
DIR="$BASE/$RUN"
mkdir -p "$BASE" && mkdir "$DIR" || exit 1
LOG="$DIR/report.txt"
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1

xorg_touch_info() {
    process=$1
    awk '$NF ~ /\/(evdev|libinput|void|tslib)_drv\.so$/ {
        sub(/^.*\//, "", $NF)
        if (!seen[$NF]++) print "xorg_input_module=" $NF
    }' "$process/maps" 2>/dev/null

    log=/var/log/Xorg.0.log
    for fd in "$process"/fd/*; do
        target=$(readlink "$fd" 2>/dev/null) || continue
        case "$target" in
            /var/log/Xorg*.log|/tmp/Xorg*.log) log=$target; break ;;
        esac
    done
    if [ ! -r "$log" ]; then
        printf '%s\n' 'xorg_log=unavailable'
        return
    fi
    printf '%s\n' 'xorg_log=readable'
    awk '
        tolower($0) ~ /not automatically adding devices/ { print "xorg_auto_add=disabled"; next }
        tolower($0) ~ /automatically adding devices/ { print "xorg_auto_add=enabled" }
        tolower($0) ~ /config\/udev: adding input device.*cyttsp5_mt/ { print "xorg_touch_added_by=udev" }
        tolower($0) ~ /config\/hal: adding input device.*cyttsp5_mt/ { print "xorg_touch_added_by=hal" }
    ' "$log"
}

check_touch_readers() {
    node=$1
    inspect_xorg=${2:-}
    found=0
    for process in /proc/[0-9]*; do
        [ -r "$process/comm" ] || continue
        for fd in "$process"/fd/*; do
            [ "$fd" -ef "$node" ] || continue
            name=$(tr -cd '[:alnum:]_. -' < "$process/comm" 2>/dev/null) || continue
            printf 'touch_reader_pid=%s comm=%s\n' "${process##*/}" "$name"
            if [ "$inspect_xorg" = xorg ] && [ "$name" = Xorg ]; then
                xorg_touch_info "$process"
            fi
            found=$((found + 1))
            break
        done
    done
    printf 'touch_readers_observed=%s\n' "$found"
}

check_xorg_touch() {
    printf '\n[xorg_touch]\n'
    for event in /sys/class/input/event*; do
        [ -r "$event/device/name" ] || continue
        [ "$(cat "$event/device/name")" = cyttsp5_mt ] || continue
        node="/dev/input/${event##*/}"
        printf 'xorg_touch_event=%s\n' "${event##*/}"
        check_touch_readers "$node" xorg
        if command -v udevadm >/dev/null 2>&1; then
            udevadm info --query=property --name "$node" 2>/dev/null |
                awk '/^ID_INPUT(_TOUCHSCREEN)?=[01]$/ {print "touch_udev_" $0}'
        else
            printf '%s\n' 'touch_udevadm=unavailable'
        fi
        return
    done
    printf '%s\n' 'xorg_touch_event=missing'
}

check_touch() {
    if [ ! -x "$HERE/touch-events" ]; then
        printf '%s\n' 'touch=touch_probe_missing'
        return
    fi
    if ! (cd "$HERE" && grep '  touch-events$' SHA256SUMS | sha256sum -c - >/dev/null 2>&1); then
        printf '%s\n' 'touch=touch_probe_checksum_failed'
        return
    fi
    for event in /sys/class/input/event*; do
        [ -r "$event/device/name" ] || continue
        [ "$(cat "$event/device/name")" = cyttsp5_mt ] || continue
        node="/dev/input/${event##*/}"
        result=$("$HERE/touch-events" "$node")
        printf 'touch=%s\n' "${result:-touch_probe_failed}"
        check_touch_readers "$node"
        return
    done
    printf '%s\n' 'touch=touch_device_missing'
}

# Who holds the EPDC module. Every open of a framebuffer node takes one module
# reference and close returns it; a mapping keeps its file, and so its
# reference, after the descriptor is closed. Only counts and comm are recorded.
check_epdc_users() {
    printf '\n[epdc_users]\n'
    users=$(awk '$1 == "mxc_epdc_v2_fb" {print $3; exit}' /proc/modules)
    printf 'epdc_module_users=%s\n' "${users:-missing}"
    for param in /sys/module/mxc_epdc_v2_fb/parameters/*; do
        [ -r "$param" ] || continue
        val=$(tr -cd '[:alnum:]_.-' < "$param" 2>/dev/null)
        printf 'epdc_param_%s=%s\n' "${param##*/}" "${val:-empty}"
    done
    if command -v initctl >/dev/null 2>&1; then
        for job in lab126_gui x awesome blanket framework; do
            st=$(initctl status "$job" 2>/dev/null | head -n 1 | tr -cd '[:alnum:]_.,/ -')
            [ -n "$st" ] && printf 'epdc_service_%s=%s\n' "$job" "$st"
        done
    fi
    for dir in /etc/upstart/ /etc/init/; do
        for job in lab126_gui x awesome blanket framework; do
            conf="${dir}${job}.conf"
            [ -r "$conf" ] || continue
            awk -v j="$job" '/^[[:space:]]*(start on|stop on|emits) / {
                sub(/^[[:space:]]+/, "", $0)
                gsub(/[^[:alnum:]_.,/ ()=-]/, "", $0)
                printf "epdc_upstart_%s=%s\n", j, $0
            }' "$conf"
        done
    done
    nodes=
    for fb in /sys/class/graphics/fb*; do
        [ -r "$fb/name" ] || continue
        printf 'epdc_fb=%s name=%s\n' "${fb##*/}" "$(tr -cd '[:alnum:]_. -' < "$fb/name")"
        for node in "/dev/${fb##*/}" "/dev/graphics/${fb##*/}"; do
            [ -e "$node" ] && nodes="$nodes $node"
        done
    done
    if [ -z "$nodes" ]; then
        printf '%s\n' 'epdc_fb_nodes=missing' 'epdc_reference_accounting=unavailable'
        return
    fi
    files=0
    mapped_only=0
    for process in /proc/[0-9]*; do
        [ -r "$process/comm" ] || continue
        fds=0
        for fd in "$process"/fd/*; do
            for node in $nodes; do
                if [ "$fd" -ef "$node" ]; then
                    fds=$((fds + 1))
                    break
                fi
            done
        done
        maps=0
        for node in $nodes; do
            count=$(awk -v node="$node" '$6 == node {++n} END {print n + 0}' "$process/maps" 2>/dev/null) || count=0
            maps=$((maps + ${count:-0}))
        done
        [ "$fds" -gt 0 ] || [ "$maps" -gt 0 ] || continue
        printf 'epdc_user_pid=%s comm=%s fds=%s mappings=%s\n' "${process##*/}" \
            "$(tr -cd '[:alnum:]_. -' < "$process/comm" 2>/dev/null)" "$fds" "$maps"
        files=$((files + fds))
        [ "$fds" -gt 0 ] || mapped_only=$((mapped_only + 1))
    done
    openers=$((files + mapped_only))
    printf 'epdc_open_fds=%s\nepdc_mapped_only_processes=%s\n' "$files" "$mapped_only"
    # Descriptors shared by dup or fork count once in the module, so more
    # observed openers than references is possible; fewer is not explained.
    case "$users" in
        ''|*[!0-9]*) accounting=unavailable ;;
        *)
            if [ "$users" -eq "$openers" ]; then
                accounting=matched
            elif [ "$users" -lt "$openers" ]; then
                accounting=shared_files_possible
            else
                accounting=more_references_than_openers
            fi
            ;;
    esac
    printf 'epdc_reference_accounting=%s\n' "$accounting"
}

check_wifi_users() {
    printf '\n[wifi_users]\n'
    awk '$1 == "bcmdhd" {
        print "wifi_module_users=" $3
        print "wifi_module_holders=" $4
        found = 1
    } END { if (!found) print "wifi_module_users=unloaded" }' /proc/modules
    for parameter in op_mode dhd_msg_level iface_name firmware_path nvram_path; do
        value="/sys/module/bcmdhd/parameters/$parameter"
        [ -r "$value" ] || continue
        case "$parameter" in
            firmware_path|nvram_path)
                if [ -n "$(cat "$value" 2>/dev/null)" ]; then
                    printf 'wifi_param_%s=configured\n' "$parameter"
                else
                    printf 'wifi_param_%s=empty\n' "$parameter"
                fi
                ;;
            *)
                printf 'wifi_param_%s=%s\n' "$parameter" "$(tr -cd '[:alnum:]_.-' < "$value" 2>/dev/null)"
                ;;
        esac
    done
    for iface in /sys/class/net/wlan*; do
        [ -d "$iface" ] || continue
        name=${iface##*/}
        state=$(cat "$iface/operstate" 2>/dev/null || printf 'unknown')
        carrier=$(cat "$iface/carrier" 2>/dev/null || printf 'unknown')
        printf 'wifi_iface=%s operstate=%s carrier=%s\n' "$name" "$state" "$carrier"
    done
    for dev in /sys/bus/sdio/drivers/bcmsdh_sdmmc/mmc*; do
        [ -e "$dev" ] || continue
        printf 'wifi_sdio_binding=%s\n' "${dev##*/}"
    done
    found_proc=0
    for process in /proc/[0-9]*; do
        [ -r "$process/comm" ] || continue
        comm=$(tr -cd '[:alnum:]_. -' < "$process/comm" 2>/dev/null) || continue
        case "$comm" in
            wpa_supplicant|wifid|udhcpc|dhclient|lipc-daemon)
                printf 'wifi_process_pid=%s comm=%s\n' "${process##*/}" "$comm"
                found_proc=$((found_proc + 1))
                ;;
        esac
    done
    printf 'wifi_processes_observed=%s\n' "$found_proc"
}

backup_runtime_fdt() {
    printf '\n[runtime_fdt]\n'
    src=/sys/firmware/fdt
    dst="$DIR/stock-runtime.dtb"
    if [ ! -r "$src" ]; then
        printf '%s\n' 'fdt_backup=unavailable'
        return
    fi
    if ! cp "$src" "$dst" 2>/dev/null; then
        printf '%s\n' 'fdt_backup=copy_failed'
        return
    fi
    size=$(wc -c < "$dst" 2>/dev/null | tr -cd '0-9')
    magic=$(od -An -tx1 -N4 "$dst" 2>/dev/null | tr -cd '0-9a-fA-F' | tr 'A-F' 'a-f')
    printf 'fdt_bytes=%s\n' "${size:-0}"
    printf 'fdt_magic=%s\n' "${magic:-unknown}"
    if [ "${size:-0}" -lt 40 ] || [ "$magic" != "d00dfeed" ]; then
        rm -f "$dst"
        printf '%s\n' 'fdt_backup=invalid_magic'
        return
    fi
    sum=$(sha256sum "$dst" 2>/dev/null | awk '{print $1}')
    printf 'fdt_sha256=%s\n' "${sum:-unavailable}"
    printf '%s\n' 'fdt_backup=ok'
}

previous_boot_recovery() {
    printf '\n[previous_boot_recovery]\n'
    if [ ! -r /proc/printkbuf ]; then
        printf '%s\n' 'previous_boot_result=buffer_unavailable'
        return
    fi
    tr '\000' '\n' < /proc/printkbuf | awk '
        match($0, /\[ *[0-9]+\.[0-9]+\]/) {
            ++lines
            time = substr($0, RSTART, RLENGTH)
            message = substr($0, RSTART + RLENGTH)
            if (message ~ /no file given for LUN[0-9]+/) {
                kind = "usb_lun_missing_medium"
            } else if (message ~ /g_mass_storage gadget: g_mass_storage ready/) {
                kind = "usb_gadget_ready"
            } else if (message ~ /input: cyttsp5_mt as /) {
                kind = "touch_input_registered"
            } else if (message ~ /cyttsp5.*failed to get HID descriptor/) {
                kind = "touch_hid_descriptor_failed"
            } else if (message ~ /cyttsp5.*Operational mode/) {
                kind = "touch_operational_mode"
            } else if (message ~ /fsg_store_file:[0-9]+, file=/) {
                kind = message ~ /file=[[:space:]]*$/ ? "usb_medium_cleared" : "usb_medium_configured"
            } else {
                next
            }
            print "previous_boot_event=" kind, time
            ++events
        }
        END {
            print "previous_boot_log_lines=" lines + 0
            print "previous_boot_recovery_events=" events + 0
            if (lines < 20)
                print "previous_boot_result=buffer_unreadable"
            else
                print "previous_boot_result=log_filtered"
        }'
}

{
    printf '%s\n' 'Readom KOA2 quiesce preflight v8'
    date -u
    uname -a
    if [ "$(uname -r)" != 4.1.15-lab126 ]; then
        printf '%s\n' 'result=refused_unknown_kernel'
    else
        printf '\n[system]\n'
        awk '$2 == "/" {print "rootfs=" $4; exit}' /proc/mounts
        printf 'cpu_online=%s\n' "$(cat /sys/devices/system/cpu/online 2>/dev/null)"

        printf '\n[drivers]\n'
        awk '$1 ~ /^(mxc_epdc_v2_fb|bcmdhd|g_mass_storage|usb_f_mass_storage|libcomposite|cyttsp5_i2c|bma2x2|als1_max44009)$/ {
            print "module=" $1, "users=" $3, "holders=" $4
        }' /proc/modules

        if [ "${1:-}" = epdc ]; then
            check_epdc_users
        elif [ "${1:-}" = wifi ]; then
            check_wifi_users
        elif [ "${1:-}" = fdt ]; then
            backup_runtime_fdt
        fi

        printf '\n[input]\n'
        for event in /sys/class/input/event*; do
            [ -r "$event/device/name" ] || continue
            printf 'input=%s name=%s\n' "${event##*/}" "$(cat "$event/device/name")"
        done
        for device in /sys/bus/i2c/drivers/cyttsp5_i2c_adapter/[0-9]*-[0-9a-f]*; do
            [ -d "$device" ] || continue
            printf 'touch_i2c_binding=%s\n' "${device##*/}"
        done
        if [ "${1:-}" = touch ]; then
            check_touch
        elif [ "${1:-}" = xorg ]; then
            check_xorg_touch
        fi

        printf '\n[usb]\n'
        for parameter in file luns ro removable cdrom nofua stall recovery_mode; do
            value="/sys/module/g_mass_storage/parameters/$parameter"
            [ -r "$value" ] || continue
            if [ "$parameter" = file ]; then
                if [ -n "$(cat "$value")" ]; then
                    printf '%s\n' 'usb_backing_file=configured'
                else
                    printf '%s\n' 'usb_backing_file=empty'
                fi
            else
                printf 'usb_%s=%s\n' "$parameter" "$(cat "$value")"
            fi
        done
        for controller in /sys/class/udc/*; do
            [ -r "$controller/state" ] || continue
            printf 'usb_udc=%s state=%s\n' "${controller##*/}" "$(cat "$controller/state")"
        done
        if [ "${1:-}" = previous-boot ]; then
            previous_boot_recovery
        fi

        printf '\n%s\n' 'result=quiesce_blocked_pending_restore'
    fi
} > "$LOG" || exit 1
printf '%s\n' "$RUN" > "$BASE/LAST_RUN.txt" || exit 1
printf 'Quiesce preflight report: %s\n' "$LOG"
