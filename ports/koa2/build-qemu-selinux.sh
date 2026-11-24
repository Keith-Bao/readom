#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Builds a QEMU vexpress-a9 SELinux policy30/xperms smoke image; not Kindle hardware.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../.." && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-selinux41
out=$base/out-qemu-selinux
art=$HOME/artifacts/qemu-selinux
mkdir -p "$out" "$art"
python3 "$here/prepare-headless-source.py" "$base/linux-4.1.15.tar.gz" "$src"
python3 "$here/prepare-selinux41.py" "$src"
# Guard stock printk's dump_lastk_to_mmc when CONFIG_LAB126_PRINTK_BUFFER is disabled on vexpress.
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
python3 "$repo/tools/merge-kconfig.py" "$out/.config" "$here/qemu-selinux.config"
make -C "$src" O="$out" olddefconfig
python3 "$repo/tools/merge-kconfig.py" --check "$out/.config" "$here/qemu-selinux.config"
make -C "$src" O="$out" -j4 zImage vexpress-v2p-ca9.dtb
checkpolicy -M -c 30 -o "$art/policy.30" "$here/qemu-selinux-policy.conf"
printf 'NOT_A_VALID_SELINUX_POLICY_BINARY' > "$art/policy.bad"
arm-linux-gnueabi-gcc -std=c11 -Wall -Wextra -Werror -O2 -static     "$here/qemu-selinux-init.c" -o "$art/init"
gcc -O2 "$src/usr/gen_init_cpio.c" -o "$art/gen_init_cpio"
cat > "$art/initramfs.list" <<EOF
dir /dev 0755 0 0
dir /proc 0755 0 0
dir /sys 0755 0 0
nod /dev/console 0600 0 0 c 5 1
file /init $art/init 0755 0 0
file /policy.30 $art/policy.30 0644 0 0
file /policy.bad $art/policy.bad 0644 0 0
EOF
"$art/gen_init_cpio" -t 0 "$art/initramfs.list" | gzip -n > "$art/initramfs.cpio.gz"
cp "$out/arch/arm/boot/zImage" "$out/arch/arm/boot/dts/vexpress-v2p-ca9.dtb" "$art/"
echo 'QEMU only. Not a Kindle boot package.' > "$art/SCOPE.txt"
