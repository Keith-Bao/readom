#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Builds the unified KOA2 Android 4.1 experimental kernel (zImage, DTB, modules)
# combining headless-smoke, multi-context Binder, SELinux policy30/xperms, and
# android41-core.config. Host-only build; never touches Kindle hardware.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-android41
out=$base/out-android41
art=$HOME/artifacts/android41-kernel
binder_snap=$HOME/artifacts/binder41-reconstructed
mkdir -p "$out" "$art"

python3 "$here/prepare-headless-source.py" "$base/linux-4.1.15.tar.gz" "$src"
if [[ ! -f $binder_snap/receipt.json ]]; then
    rm -rf "$binder_snap"
    python3 "$here/reconstruct-binder.py" "$binder_snap"
fi
python3 "$here/wire-binder41.py" "$binder_snap" "$src"
python3 "$here/prepare-selinux41.py" "$src"

export ARCH=arm CROSS_COMPILE=$HOME/toolchains/aosp-arm-gcc49/bin/arm-linux-androidkernel-
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=koa2-android41 KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP='Tue Aug 1 11:50:12 UTC 2023'
export KCFLAGS='-fno-pic -fno-pie'
make() { command make HOSTCFLAGS='-O2 -fcommon' "$@"; }

if [[ ! -f $out/.config ]]; then
    make -C "$src" O="$out" imx_v7_zelda_defconfig > "$art/configure.log" 2>&1
fi
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/android41-core.config"
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/binder41.config"
# Keep Falcon wrapper off so standard sdhci-esdhc-imx owns eMMC.
"$src/scripts/config" --file "$out/.config" --disable FALCON_WRAPPER --disable MXC_GPU_VIV
make -C "$src" O="$out" olddefconfig > "$art/oldconfig.log" 2>&1
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/android41-core.config" | tee "$art/config-check.log"
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/binder41.config" | tee -a "$art/config-check.log"

make -C "$src" O="$out" -j4 zImage imx7d-zelda.dtb modules
cp "$out/.config" "$art/kernel.config"
cp "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/imx7d-zelda.dtb" "$art/"
sha256sum "$art/zImage" "$art/imx7d-zelda.dtb" > "$art/SHA256SUMS"
echo 'Unified KOA2 Android 4.1 experimental kernel (host build only; not deployed).' > "$art/SCOPE.txt"
cat "$art/SHA256SUMS"
