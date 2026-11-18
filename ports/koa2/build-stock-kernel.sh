#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Host-only build. Does not access Kindle storage or load any modules.
set -euo pipefail
[[ $(uname -s) == Linux ]] || exit 1
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-stock
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 "$script_dir/check-kernel-source.py" "$src"
out=$base/out-stock
art=$HOME/artifacts/stock-kernel
tc=$HOME/toolchains/aosp-arm-gcc49/bin/arm-linux-androidkernel-
printf '%s  %s\n' efb1b966fedcba318af381080f6a699642a66a929c42e8578aeb6e3fe240ea93 "$base/linux-4.1.15.tar.gz" | sha256sum -c -
printf '%s  %s\n' 0d273adc4ac4672fe67a99965bf4ce3814784433d0ac2d80689ec28d49d11c25 /mnt/e/Readom-Downloads/aosp-arm-gcc49.tar.gz | sha256sum -c -
mkdir -p "$out" "$art"
export ARCH=arm CROSS_COMPILE="$tc"
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=koa2-build KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP='Tue Aug 1 11:50:12 UTC 2023'
export KCFLAGS='-fno-pic -fno-pie'
# Linux 4.1 dtc predates GCC 10's -fno-common default (duplicate yylloc).
# Host tooling only; this does not change target ARM driver code.
export HOSTCFLAGS='-O2 -fcommon'
make() { command make HOSTCFLAGS="$HOSTCFLAGS" "$@"; }
"${tc}gcc" --version > "$art/compiler.txt"
make -C "$src" O="$out" imx_v7_zelda_defconfig 2>&1 | tee "$art/configure.log"
make -C "$src" O="$out" -j4 zImage imx7d-zelda.dtb modules 2>&1 | tee "$art/build.log"
cp "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/imx7d-zelda.dtb" "$art/"
cp "$out/.config" "$art/kernel.config"
cp "$out/System.map" "$out/Module.symvers" "$art/"
make -C "$src" O="$out" INSTALL_MOD_PATH="$art/modules" modules_install 2>&1 | tee "$art/modules.log"
find "$art" -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > "$art/SHA256SUMS"
