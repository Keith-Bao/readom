#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Builds the unified KOA2 Android 4.1 experimental kernel WITH the clean-room
# epdc-v1 Lab126 helpers (mxc_epdc_v2_fb.ko linked and modpost-checked, night
# mode hard-rejected). Host-only build in an isolated work/out directory;
# never modifies linux-stock, linux-headless-smoke, or linux-android41.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-android41-epdc
out=$base/out-android41-epdc
art=$HOME/artifacts/android41-epdc-kernel
binder_snap=$HOME/artifacts/binder41-reconstructed
mkdir -p "$out" "$art"

python3 "$here/prepare-headless-source.py" "$base/linux-4.1.15.tar.gz" "$src"
if [[ ! -f $binder_snap/receipt.json ]]; then
    rm -rf "$binder_snap"
    python3 "$here/reconstruct-binder.py" "$binder_snap"
fi
python3 "$here/wire-binder41.py" "$binder_snap" "$src"
python3 "$here/prepare-selinux41.py" "$src"

# Wire clean-room epdc-v1 helper + night-mode hard rejection guard.
cp "$here/epdc-v1/mxc_epdc_fb_lab126.c" "$src/drivers/video/fbdev/mxc/mxc_epdc_fb_lab126.c"
python3 - "$src/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.c" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
t = p.read_text()
old = """#if defined(CONFIG_LAB126) && defined(EINK_NIGHTMODE)
	if (upd_data->waveform_mode == WAVEFORM_MODE_GCK16 ||
		upd_data->waveform_mode == WAVEFORM_MODE_GLKW16)
		fb_data->night_mode = upd_data->waveform_mode;
	else
		fb_data->night_mode = 0;

	/* should not have to do this if UI setup mode correctly */
	if (fb_data->epdc_fb_var.grayscale == GRAYSCALE_8BIT_INVERTED) {
		if (upd_data->waveform_mode == WAVEFORM_MODE_GLR16)
			upd_data->waveform_mode = WAVEFORM_MODE_GLKW16;
		else if (upd_data->waveform_mode == WAVEFORM_MODE_GC16)
			upd_data->waveform_mode = WAVEFORM_MODE_GCK16;
	}
#endif"""
new = """#if defined(CONFIG_LAB126) && defined(EINK_NIGHTMODE)
	/* epdc-v1 safety gate: reject night-mode waveforms until 0x04 VDC is decoded. */
	fb_data->night_mode = 0;
	if (upd_data->waveform_mode == WAVEFORM_MODE_GCK16 ||
	    upd_data->waveform_mode == WAVEFORM_MODE_GLKW16 ||
	    fb_data->epdc_fb_var.grayscale == GRAYSCALE_8BIT_INVERTED) {
		dev_warn(fb_data->dev, "epdc-v1: refusing night-mode update (mode=0x%x)\\n",
			 upd_data->waveform_mode);
		return -EOPNOTSUPP;
	}
#endif"""
if new not in t:
    assert t.count(old) == 1, 'unexpected night_mode block in mxc_epdc_v2_fb.c'
    p.write_text(t.replace(old, new, 1))
PY

export ARCH=arm CROSS_COMPILE=$HOME/toolchains/aosp-arm-gcc49/bin/arm-linux-androidkernel-
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=koa2-android41-epdc KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP='Tue Aug 1 11:50:12 UTC 2023'
export KCFLAGS='-fno-pic -fno-pie'
make() { command make HOSTCFLAGS='-O2 -fcommon' "$@"; }

if [[ ! -f $out/.config ]]; then
    make -C "$src" O="$out" imx_v7_zelda_defconfig > "$art/configure.log" 2>&1
fi
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/android41-core.config"
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/binder41.config"
"$src/scripts/config" --file "$out/.config" \
    --disable FALCON_WRAPPER --disable MXC_GPU_VIV \
    --module FB_MXC_EINK_V2_PANEL --disable FB_MXC_EINK_PANEL
make -C "$src" O="$out" olddefconfig > "$art/oldconfig.log" 2>&1
grep -v 'CONFIG_FB_MXC_EINK_V2_PANEL' "$here/android41-core.config" > "$art/android41-epdc-check.config"
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$art/android41-epdc-check.config" | tee "$art/config-check.log"
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/binder41.config" | tee -a "$art/config-check.log"
grep -q '^CONFIG_FB_MXC_EINK_V2_PANEL=m$' "$out/.config"

make -C "$src" O="$out" -j4 zImage imx7d-zelda.dtb modules
test -f "$out/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.ko"
cp "$out/.config" "$art/kernel.config"
cp "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/imx7d-zelda.dtb" \
   "$out/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.ko" "$art/"
sha256sum "$art/zImage" "$art/imx7d-zelda.dtb" "$art/mxc_epdc_v2_fb.ko" > "$art/SHA256SUMS"
echo 'Unified KOA2 Android 4.1 + epdc-v1 experimental kernel (host build only; night mode rejected; not deployed).' > "$art/SCOPE.txt"
cat "$art/SHA256SUMS"
