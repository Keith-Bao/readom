#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Compiles drivers/video/fbdev/mxc/mxc_epdc_v2_fb.o in an isolated research tree
# (~/readom-work/linux-epdc-v1) using the clean-room v1 Lab126 helpers and the
# hard night-mode rejection guard. Never modifies linux-stock or headless-smoke.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-epdc-v1
out=$base/out-epdc-v1
mkdir -p "$out"

if [[ ! -f $src/.koa2-epdc-v1-source ]]; then
    rm -rf "$src"
    mkdir -p "$src"
    tar --no-same-owner -xzf "$base/linux-4.1.15.tar.gz" -C "$src"
    printf 'epdc-v1\n' > "$src/.koa2-epdc-v1-source"
fi

cp "$here/mxc_epdc_fb_lab126.c" "$src/drivers/video/fbdev/mxc/mxc_epdc_fb_lab126.c"

# Enforce night-mode waveform rejection at update submission (ports/koa2/epdc-source-gap.md).
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
export KCFLAGS='-fno-pic -fno-pie'
make() { command make HOSTCFLAGS='-O2 -fcommon' "$@"; }
if [[ ! -f $out/.config ]]; then
    make -C "$src" O="$out" imx_v7_zelda_defconfig
fi
make -C "$src" O="$out" prepare modules_prepare
make -C "$src" O="$out" drivers/video/fbdev/mxc/mxc_epdc_v2_fb.o
echo "EPDC v1 compile check passed: $out/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.o"
