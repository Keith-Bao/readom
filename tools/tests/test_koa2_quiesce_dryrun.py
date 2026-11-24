#!/usr/bin/env python3
"""Run the KOA2 quiesce preflight against a simulated stock system."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "ports/koa2/stage-test/quiesce-dryrun.sh"
GIT_BASH = Path(r"C:\Program Files\Git\bin\bash.exe")


def shell_path(path):
    if os.name == "nt":
        return f"/{path.drive[0].lower()}/{path.as_posix()[3:]}"
    return str(path)


def check_preflight(release, expected_result, touch=False, previous_log=None, xorg=False):
    with tempfile.TemporaryDirectory(dir=ROOT if os.name == "nt" else None) as temp:
        root = Path(temp)
        files = {
            "proc/version": f"Linux {release}\n",
            "proc/mounts": "/dev/root / ext4 ro,noatime 0 0\n",
            "proc/modules": "mxc_epdc_v2_fb 1 3 - Live 0\ncyttsp5_i2c 1 0 - Live 0\n",
            "sys/devices/system/cpu/online": "0-1\n",
            "sys/devices/system/cpu/cpu1/online": "1\n",
            "sys/class/input/event3/device/name": "cyttsp5_mt\n",
            "sys/module/g_mass_storage/parameters/file": "/dev/mmcblk2p7\n",
            "sys/module/g_mass_storage/parameters/luns": "2\n",
            "sys/class/udc/ci_hdrc.0/state": "not attached\n",
        }
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, newline="\n")
        (root / "mnt/us/documents").mkdir(parents=True)
        (root / "sys/bus/i2c/drivers/cyttsp5_i2c_adapter/1-0024").mkdir(parents=True)
        if previous_log is not None:
            (root / "proc/printkbuf").write_bytes(previous_log)
        fake_set = shell_path(root)
        commands = f'''uname() {{ printf '%s\\n' '{release}'; }}
rmmod() {{ echo rmmod >> "{fake_set}/mutations"; }}
modprobe() {{ echo modprobe >> "{fake_set}/mutations"; }}
mount() {{ echo mount >> "{fake_set}/mutations"; }}
'''
        text = SCRIPT.read_text()
        for prefix in ("/mnt/us", "/proc/", "/sys/", "/dev/input/", "/var/log/"):
            text = text.replace(prefix, fake_set + prefix)
        script = root / "preflight.sh"
        if touch or xorg:
            node = root / "dev/input/event3"
            node.parent.mkdir(parents=True)
            node.touch()
            reader = root / "proc/111"
            (reader / "fd").mkdir(parents=True)
            (reader / "comm").write_text("Xorg\n" if xorg else "stock-input\n")
            (reader / "fd/6").symlink_to(node)
            (reader / "fd/7").symlink_to(node)
            (reader / "fd/8").symlink_to(root / "removed-node")
        if touch:
            probe = root / "touch-events"
            probe.write_text(f'#!/bin/sh\n[ "$1" = "{fake_set}/dev/input/event3" ] || exit 2\necho touch_contact_observed\n', newline="\n")
            probe.chmod(0o755)
            digest = hashlib.sha256(probe.read_bytes()).hexdigest()
            (root / "SHA256SUMS").write_text(f"{digest}  touch-events\n", newline="\n")
        if xorg:
            (reader / "maps").write_text("123-456 r-xp 00000000 00:00 0 /stock/evdev_drv.so\n")
            logs = root / "var/log"
            logs.mkdir(parents=True)
            (logs / "Xorg.0.log").write_text(
                "[30.00] (**) Not automatically adding devices\n"
                "[31.00] (II) config/udev: Adding input device cyttsp5_mt (/dev/input/event3)\n"
                "[32.00] this unrelated line must stay private\n"
            )
            commands += "udevadm() { printf '%s\\n' ID_INPUT=1 ID_INPUT_TOUCHSCREEN=1 ID_SERIAL=private; }\n"
        script.write_text(commands + text, newline="\n")
        shell = str(GIT_BASH) if os.name == "nt" and GIT_BASH.exists() else shutil.which("sh")
        command = [shell, shell_path(script)]
        if touch:
            command.append("touch")
        elif previous_log is not None:
            command.append("previous-boot")
        elif xorg:
            command.append("xorg")
        subprocess.run(command, check=True, timeout=20)
        base = root / "mnt/us/koa2-quiesce-dryrun"
        report = (base / (base / "LAST_RUN.txt").read_text().strip() / "report.txt").read_text()
        assert report.splitlines()[-1] == f"result={expected_result}", report
        assert not (root / "mutations").exists(), "Preflight issued a mutating command"
        for name, content in files.items():
            assert (root / name).read_text() == content, name
        if release == "4.1.15-lab126":
            assert "module=mxc_epdc_v2_fb users=3" in report, report
            assert "input=event3 name=cyttsp5_mt" in report, report
            assert "touch_i2c_binding=1-0024" in report, report
            assert "usb_backing_file=configured" in report, report
            assert "usb_luns=2" in report, report
            assert "usb_udc=ci_hdrc.0 state=not attached" in report, report
            assert ("touch=touch_contact_observed" in report) == touch, report
            if touch:
                assert "touch_reader_pid=111 comm=stock-input" in report, report
                assert "touch_readers_observed=1" in report, report
            if xorg:
                for expected in ("touch_reader_pid=111 comm=Xorg", "xorg_input_module=evdev_drv.so",
                                 "xorg_auto_add=disabled", "xorg_touch_added_by=udev",
                                 "touch_udev_ID_INPUT_TOUCHSCREEN=1"):
                    assert expected in report, report
                assert "private" not in report, report
            assert "/dev/mmcblk2p7" not in report, report
            if previous_log:
                assert "previous_boot_event=usb_lun_missing_medium [ 23.001]" in report, report
                assert "previous_boot_event=touch_input_registered [ 24.001]" in report, report
                assert "previous_boot_event=usb_medium_configured [ 25.001]" in report, report
                assert "previous_boot_result=log_filtered" in report, report
                assert "/private/runtime" not in report, report
            elif previous_log is not None:
                assert "previous_boot_result=buffer_unreadable" in report, report
        else:
            assert "[drivers]" not in report, report


def check_touch_frames(compiler):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        source = root / "frames.c"
        binary = root / "frames"
        source.write_text('''
#define main touch_probe_main
#include "touch-events.c"
#undef main
#include <assert.h>

static bool event(struct touch_frame *frame, unsigned type, unsigned code, int value)
{
    const struct input_event input = {.type = type, .code = code, .value = value};
    return touch_reported(frame, &input);
}

int main(void)
{
    struct touch_frame frame = {0};

    assert(!event(&frame, EV_ABS, ABS_MT_TRACKING_ID, -1));
    assert(!event(&frame, EV_ABS, ABS_MT_POSITION_X, 120));
    assert(!event(&frame, EV_SYN, SYN_REPORT, 0));
    assert(!event(&frame, EV_ABS, ABS_MT_TRACKING_ID, 1));
    assert(!event(&frame, EV_SYN, SYN_DROPPED, 0));
    assert(!event(&frame, EV_KEY, BTN_TOUCH, 1));
    assert(!event(&frame, EV_SYN, SYN_REPORT, 0));
    assert(!event(&frame, EV_ABS, ABS_MT_TRACKING_ID, 2));
    assert(event(&frame, EV_SYN, SYN_REPORT, 0));
    assert(!event(&frame, EV_SYN, SYN_REPORT, 0));
    assert(!event(&frame, EV_KEY, BTN_TOUCH, 1));
    assert(event(&frame, EV_SYN, SYN_REPORT, 0));
    return 0;
}
''', encoding="utf-8")
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(SCRIPT.parent), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=5)


check_preflight("4.1.15-lab126", "quiesce_blocked_pending_restore")
check_preflight("4.9.0-other", "refused_unknown_kernel")
check_preflight("4.1.15-lab126", "quiesce_blocked_pending_restore", previous_log=b"")
check_preflight("4.1.15-lab126", "quiesce_blocked_pending_restore", previous_log=(
    b"[ 1.001] ignored\n" * 20 +
    b"\0 [ 23.001] no file given for LUN0\n"
    b"[ 24.001] input: cyttsp5_mt as /private/runtime/input\n"
    b"[ 25.001] fsg_store_file:448, file=/private/runtime/medium\n"
))
if os.name != "nt":
    check_preflight("4.1.15-lab126", "quiesce_blocked_pending_restore", touch=True)
    check_preflight("4.1.15-lab126", "quiesce_blocked_pending_restore", xorg=True)
    compiler = shutil.which("cc")
    if compiler:
        check_touch_frames(compiler)

def check_epdc(users, expected, nodes=True):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        files = {
            "proc/version": "Linux 4.1.15-lab126\n",
            "proc/mounts": "/dev/root / ext4 ro,noatime 0 0\n",
            "proc/modules": f"mxc_epdc_v2_fb 1 {users} - Live 0\n",
            "sys/devices/system/cpu/online": "0-1\n",
            "sys/class/graphics/fb0/name": "mxc_epdc_fb\n",
        }
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, newline="\n")
        (root / "mnt/us/documents").mkdir(parents=True)
        fb = root / "dev/fb0"
        if nodes:
            fb.parent.mkdir(parents=True)
            fb.touch()
        line = f"40000000-40100000 rw-s 00000000 00:05 1 {fb}\n"
        for pid, comm, fds, maps in ((111, "Xorg", 1, 1), (222, "awesome", 1, 0),
                                     (333, "mapper", 0, 2), (444, "other", 0, 0)):
            process = root / f"proc/{pid}"
            (process / "fd").mkdir(parents=True)
            (process / "comm").write_text(comm + "\n")
            (process / "maps").write_text(line * maps + "1000-2000 r-xp 0 00:00 0 /bin/sh\n")
            for fd in range(fds):
                (process / f"fd/{fd + 3}").symlink_to(fb)
            (process / "fd/9").symlink_to(root / "proc/version")
        commands = f'''uname() {{ printf '%s\n' 4.1.15-lab126; }}
rmmod() {{ echo rmmod >> "{root}/mutations"; }}
modprobe() {{ echo modprobe >> "{root}/mutations"; }}
mount() {{ echo mount >> "{root}/mutations"; }}
'''
        text = SCRIPT.read_text()
        for prefix in ("/mnt/us", "/proc/", "/sys/", "/dev/input/", "/var/log/", '"/dev/'):
            text = text.replace(prefix, prefix[0] + str(root) + prefix[1:] if prefix[0] == '"'
                                else str(root) + prefix)
        script = root / "preflight.sh"
        script.write_text(commands + text, newline="\n")
        subprocess.run([shutil.which("sh"), str(script), "epdc"], check=True, timeout=20)
        base = root / "mnt/us/koa2-quiesce-dryrun"
        report = (base / (base / "LAST_RUN.txt").read_text().strip() / "report.txt").read_text()
        lines = report.splitlines()
        assert lines[-1] == "result=quiesce_blocked_pending_restore", report
        assert not (root / "mutations").exists(), "Preflight issued a mutating command"
        assert f"epdc_module_users={users}" in lines, report
        assert f"epdc_reference_accounting={expected}" in lines, report
        if nodes:
            for wanted in ("epdc_fb=fb0 name=mxc_epdc_fb",
                           "epdc_user_pid=111 comm=Xorg fds=1 mappings=1",
                           "epdc_user_pid=222 comm=awesome fds=1 mappings=0",
                           "epdc_user_pid=333 comm=mapper fds=0 mappings=2",
                           "epdc_open_fds=2", "epdc_mapped_only_processes=1"):
                assert wanted in lines, (wanted, report)
            assert "pid=444" not in report, report
        else:
            assert "epdc_fb_nodes=missing" in lines, report
        for name, content in files.items():
            assert (root / name).read_text() == content, name




def check_wifi():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        files = {
            "proc/version": "Linux 4.1.15-lab126\n",
            "proc/mounts": "/dev/root / ext4 ro,noatime 0 0\n",
            "proc/modules": "bcmdhd 500000 0 - Live 0\n",
            "sys/devices/system/cpu/online": "0-1\n",
            "sys/module/bcmdhd/parameters/op_mode": "0\n",
            "sys/module/bcmdhd/parameters/firmware_path": "/lib/firmware/brcm/fw.bin\n",
            "sys/class/net/wlan0/operstate": "up\n",
            "sys/class/net/wlan0/carrier": "1\n",
            "sys/class/net/wlan0/address": ":".join(("aa", "bb", "cc", "dd", "ee", "ff")) + "\n",
        }
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, newline="\n")
        (root / "mnt/us/documents").mkdir(parents=True)
        (root / "sys/bus/sdio/drivers/bcmsdh_sdmmc/mmc0:0001:1").mkdir(parents=True)
        for pid, comm in ((501, "wpa_supplicant"), (502, "wifid"), (503, "other")):
            proc = root / f"proc/{pid}"
            proc.mkdir(parents=True)
            (proc / "comm").write_text(comm + "\n")
        commands = f"""uname() {{ printf '%s\n' 4.1.15-lab126; }}
rmmod() {{ echo rmmod >> "{root}/mutations"; }}
modprobe() {{ echo modprobe >> "{root}/mutations"; }}
mount() {{ echo mount >> "{root}/mutations"; }}
"""
        text = SCRIPT.read_text()
        for prefix in ("/mnt/us", "/proc/", "/sys/", "/dev/input/", "/var/log/"):
            text = text.replace(prefix, str(root) + prefix)
        script = root / "preflight.sh"
        script.write_text(commands + text, newline="\n")
        subprocess.run([shutil.which("sh"), str(script), "wifi"], check=True, timeout=20)
        base = root / "mnt/us/koa2-quiesce-dryrun"
        report = (base / (base / "LAST_RUN.txt").read_text().strip() / "report.txt").read_text()
        lines = report.splitlines()
        assert lines[-1] == "result=quiesce_blocked_pending_restore", report
        assert not (root / "mutations").exists()
        for wanted in ("wifi_module_users=0", "wifi_param_op_mode=0",
                       "wifi_param_firmware_path=configured",
                       "wifi_iface=wlan0 operstate=up carrier=1",
                       "wifi_sdio_binding=mmc0:0001:1",
                       "wifi_process_pid=501 comm=wpa_supplicant",
                       "wifi_process_pid=502 comm=wifid",
                       "wifi_processes_observed=2"):
            assert wanted in lines, (wanted, report)
        assert ":".join(("aa", "bb", "cc", "dd", "ee", "ff")) not in report, "MAC address leaked"
        assert "pid=503" not in report, report


def check_fdt(payload, expected):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        files = {
            "proc/version": "Linux 4.1.15-lab126\n",
            "proc/mounts": "/dev/root / ext4 ro,noatime 0 0\n",
            "proc/modules": "mxc_epdc_v2_fb 1 3 - Live 0\n",
            "sys/devices/system/cpu/online": "0-1\n",
        }
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, newline="\n")
        (root / "mnt/us/documents").mkdir(parents=True)
        if payload is not None:
            fdt = root / "sys/firmware/fdt"
            fdt.parent.mkdir(parents=True, exist_ok=True)
            fdt.write_bytes(payload)
        commands = f"""uname() {{ printf '%s\n' 4.1.15-lab126; }}
rmmod() {{ echo rmmod >> "{root}/mutations"; }}
modprobe() {{ echo modprobe >> "{root}/mutations"; }}
mount() {{ echo mount >> "{root}/mutations"; }}
"""
        text = SCRIPT.read_text()
        for prefix in ("/mnt/us", "/proc/", "/sys/", "/dev/input/", "/var/log/"):
            text = text.replace(prefix, str(root) + prefix)
        script = root / "preflight.sh"
        script.write_text(commands + text, newline="\n")
        subprocess.run([shutil.which("sh"), str(script), "fdt"], check=True, timeout=20)
        base = root / "mnt/us/koa2-quiesce-dryrun"
        run_dir = base / (base / "LAST_RUN.txt").read_text().strip()
        report = (run_dir / "report.txt").read_text()
        lines = report.splitlines()
        assert lines[-1] == "result=quiesce_blocked_pending_restore", report
        assert not (root / "mutations").exists()
        assert f"fdt_backup={expected}" in lines, report
        if expected == "ok":
            copied = (run_dir / "stock-runtime.dtb").read_bytes()
            assert copied == payload
            assert f"fdt_sha256={hashlib.sha256(payload).hexdigest()}" in lines, report
        else:
            assert not (run_dir / "stock-runtime.dtb").exists()


if os.name != "nt":
    check_epdc(3, "matched")
    check_epdc(5, "more_references_than_openers")
    check_epdc(2, "shared_files_possible")
    check_epdc(3, "unavailable", nodes=False)
    check_wifi()
    check_fdt(b"\xd0\x0d\xfe\xed" + b"\x00" * 60, "ok")
    check_fdt(b"\x00\x00\x00\x00" + b"\x00" * 60, "invalid_magic")
    check_fdt(None, "unavailable")

print("KOA2 quiesce preflight: recorded stock state and EPDC users without changing them and refused another kernel")
