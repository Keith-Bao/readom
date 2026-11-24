#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Builds a QEMU vexpress-a9 smoke image with BOTH multi-context Binder and SELinux policy30.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-android41
out=$base/out-qemu-android41
art=$HOME/artifacts/qemu-android41
binder_snap=$HOME/artifacts/binder41-reconstructed
mkdir -p "$out" "$art"

python3 "$here/prepare-headless-source.py" "$base/linux-4.1.15.tar.gz" "$src"
if [[ ! -f $binder_snap/receipt.json ]]; then
    rm -rf "$binder_snap"
    python3 "$here/reconstruct-binder.py" "$binder_snap"
fi
python3 "$here/wire-binder41.py" "$binder_snap" "$src"
python3 "$here/prepare-selinux41.py" "$src"
python3 - "$src/kernel/printk/printk.c" <<'PY'
import sys
from pathlib import Path
p = Path(sys.argv[1])
t = p.read_text()
start = 'extern void __dump_lastk_to_mmc(char *buf, unsigned long start, unsigned int size);'
end = 'EXPORT_SYMBOL(dump_lastk_to_mmc);'
guard = '#ifdef CONFIG_LAB126_PRINTK_BUFFER\n' + start
if guard not in t:
    assert t.count(start) == 1 and t.count(end) == 1
    p.write_text(t.replace(start, guard).replace(end, end + '\n#endif /* CONFIG_LAB126_PRINTK_BUFFER */'))
PY

export ARCH=arm CROSS_COMPILE=$HOME/toolchains/aosp-arm-gcc49/bin/arm-linux-androidkernel-
export KBUILD_BUILD_USER=builder KBUILD_BUILD_HOST=koa2-qemu KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_TIMESTAMP='Tue Aug 1 11:50:12 UTC 2023'
export KCFLAGS='-fno-pic -fno-pie'
make() { command make HOSTCFLAGS='-O2 -fcommon' "$@"; }

if [[ ! -f $out/.config ]]; then
    make -C "$src" O="$out" vexpress_defconfig
fi
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/qemu-android41.config"
make -C "$src" O="$out" olddefconfig
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/qemu-android41.config"
make -C "$src" O="$out" -j4 zImage vexpress-v2p-ca9.dtb

checkpolicy -M -c 30 -o "$art/policy.30" "$here/qemu-selinux-policy.conf"
arm-linux-gnueabi-gcc -std=c11 -DBINDER_IPC_32BIT -Wall -Wextra -Werror -O2 -static \
    -I"$src/drivers/android" "$here/qemu-android41-init.c" -o "$art/init"
gcc -O2 "$src/usr/gen_init_cpio.c" -o "$art/gen_init_cpio"
cat > "$art/initramfs.list" <<EOF
dir /dev 0755 0 0
dir /proc 0755 0 0
dir /sys 0755 0 0
nod /dev/console 0600 0 0 c 5 1
file /init $art/init 0755 0 0
file /policy.30 $art/policy.30 0644 0 0
EOF
"$art/gen_init_cpio" -t 0 "$art/initramfs.list" | gzip -n > "$art/initramfs.cpio.gz"
cp "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/vexpress-v2p-ca9.dtb" "$art/"
echo 'QEMU only (Binder + SELinux policy30). Not a Kindle boot package.' > "$art/SCOPE.txt"
