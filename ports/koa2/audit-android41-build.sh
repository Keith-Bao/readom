#!/bin/bash
# Native 4.1 interface compile audit only; does not export a bootable package.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=${KOA2_AUDIT_SOURCE:-$base/linux-headless-smoke}
out=${KOA2_AUDIT_OUTPUT:-$base/out-android41-audit}
art=${KOA2_AUDIT_ARTIFACTS:-$HOME/artifacts/android41-audit}
mkdir -p "$out" "$art"
python3 "$here/prepare-headless-source.py" "$base/linux-4.1.15.tar.gz" "$src"
export ARCH=arm CROSS_COMPILE=$HOME/toolchains/aosp-arm-gcc49/bin/arm-linux-androidkernel-
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=koa2-build KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP='Tue Aug 1 11:50:12 UTC 2023'
export KCFLAGS='-fno-pic -fno-pie'
make() { command make HOSTCFLAGS='-O2 -fcommon' "$@"; }
make -C "$src" O="$out" imx_v7_zelda_defconfig > "$art/configure.log" 2>&1
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/android41-core.config"
if [[ -n ${KOA2_AUDIT_EXTRA_CONFIG:-} ]]; then
    python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$KOA2_AUDIT_EXTRA_CONFIG"
fi
make -C "$src" O="$out" olddefconfig > "$art/oldconfig.log" 2>&1
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/android41-core.config" | tee "$art/config-check.log"
if [[ -n ${KOA2_AUDIT_EXTRA_CONFIG:-} ]]; then
    python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$KOA2_AUDIT_EXTRA_CONFIG" | tee "$art/extra-config-check.log"
fi
make -C "$src" O="$out" -j4 kernel/seccomp.o kernel/nsproxy.o \
    drivers/android/binder.o drivers/staging/android/ashmem.o \
    drivers/staging/android/lowmemorykiller.o security/selinux/ \
    2>&1 | tee "$art/compile.log"
cp "$out/.config" "$art/kernel.config"
echo 'Object compilation only; no multi-context Binder or complete Android runtime validation.' > "$art/SCOPE.txt"
