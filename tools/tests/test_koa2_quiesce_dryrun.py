#!/usr/bin/env python3
"""Run the KOA2 quiesce preflight against a simulated stock system."""
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


def check_preflight(release, expected_result):
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
        }
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, newline="\n")
        (root / "mnt/us/documents").mkdir(parents=True)
        (root / "sys/bus/i2c/drivers/cyttsp5_i2c_adapter/1-0024").mkdir(parents=True)
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
        shell = str(GIT_BASH) if os.name == "nt" and GIT_BASH.exists() else shutil.which("sh")
        subprocess.run([shell, shell_path(script)], check=True, timeout=20)
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
            assert "/dev/mmcblk2p7" not in report, report
        else:
            assert "[drivers]" not in report, report


check_preflight("4.1.15-lab126", "quiesce_blocked_pending_restore")
check_preflight("4.9.0-other", "refused_unknown_kernel")
print("KOA2 quiesce preflight: recorded stock state without changing it and refused another kernel")
