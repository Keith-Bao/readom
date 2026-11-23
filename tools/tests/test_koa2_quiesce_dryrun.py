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


def check_preflight(release, expected_result, touch=False, previous_log=None):
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
        for prefix in ("/mnt/us", "/proc/", "/sys/"):
            text = text.replace(prefix, fake_set + prefix)
        script = root / "preflight.sh"
        script.write_text(commands + text, newline="\n")
        if touch:
            probe = root / "touch-events"
            probe.write_text('#!/bin/sh\n[ "$1" = /dev/input/event3 ] || exit 2\necho touch_contact_observed\n', newline="\n")
            probe.chmod(0o755)
            digest = hashlib.sha256(probe.read_bytes()).hexdigest()
            (root / "SHA256SUMS").write_text(f"{digest}  touch-events\n", newline="\n")
        shell = str(GIT_BASH) if os.name == "nt" and GIT_BASH.exists() else shutil.which("sh")
        command = [shell, shell_path(script)]
        if touch:
            command.append("touch")
        elif previous_log is not None:
            command.append("previous-boot")
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
    compiler = shutil.which("cc")
    if compiler:
        check_touch_frames(compiler)
print("KOA2 quiesce preflight: recorded stock state without changing it and refused another kernel")
