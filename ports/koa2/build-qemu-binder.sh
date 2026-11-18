#!/bin/bash
# Builds only a QEMU vexpress-a9 smoke image; not Kindle hardware.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-binder41
out=$base/out-qemu-binder
art=$HOME/artifacts/qemu-binder
mkdir -p "$out" "$art"
python3 "$here/prepare-qemu-source.py" "$src"
export ARCH=arm CROSS_COMPILE=$HOME/toolchains/aosp-arm-gcc49/bin/arm-linux-androidkernel-
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=koa2-qemu KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP='Tue Aug 1 11:50:12 UTC 2023'
export KCFLAGS='-fno-pic -fno-pie'
make() { command make HOSTCFLAGS='-O2 -fcommon' "$@"; }
if [[ ! -f $out/.config ]]; then
    make -C "$src" O="$out" vexpress_defconfig
fi
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/qemu-binder.config"
make -C "$src" O="$out" olddefconfig
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/qemu-binder.config"
make -C "$src" O="$out" -j4 zImage vexpress-v2p-ca9.dtb
arm-linux-gnueabi-gcc -std=c11 -DBINDER_IPC_32BIT -Wall -Wextra -Werror -O2 -static \
    -I"$src/drivers/android" "$here/qemu-binder-init.c" -o "$art/init"
gcc -O2 "$src/usr/gen_init_cpio.c" -o "$art/gen_init_cpio"
printf 'dir /dev 0755 0 0\ndir /proc 0755 0 0\ndir /sys 0755 0 0\nnod /dev/console 0600 0 0 c 5 1\nfile /init %s 0755 0 0\n' "$art/init" > "$art/initramfs.list"
"$art/gen_init_cpio" -t 0 "$art/initramfs.list" | gzip -n > "$art/initramfs.cpio.gz"
cp "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/vexpress-v2p-ca9.dtb" "$art/"
echo 'QEMU only. Not a Kindle boot package.' > "$art/SCOPE.txt"
