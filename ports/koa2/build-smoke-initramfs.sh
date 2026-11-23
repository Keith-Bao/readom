#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Host-only: builds the KOA2 smoke initramfs. Does not touch a Kindle.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
base=${KOA2_WORK_ROOT:-$HOME/readom-work}
src=$base/linux-headless-smoke
art=$HOME/artifacts/smoke-initramfs
mkdir -p "$art"
arm-linux-gnueabi-gcc -std=c11 -Wall -Wextra -Werror -O2 -static "$here/smoke-init.c" -o "$art/init"
gcc -O2 "$src/usr/gen_init_cpio.c" -o "$art/gen_init_cpio"
printf 'dir /dev 0755 0 0\ndir /proc 0555 0 0\ndir /sys 0555 0 0\nnod /dev/console 0600 0 0 c 5 1\nfile /init %s 0755 0 0\n' "$art/init" > "$art/initramfs.list"
# Build it to the same bytes every time. Three timestamps leak otherwise, and
# the kit pins this file by SHA256, so a hash that moves on its own cannot be
# told apart from contents that changed (ports/koa2/smoke-initramfs.md).
#   gzip -n            the gzip header
#   gen_init_cpio -t 0 the cpio headers of directories and device nodes
#   touch -d @0        the cpio header of /init: -t does NOT cover file entries,
#                      which take the source file's own mtime (cpio_mkfile in
#                      usr/gen_init_cpio.c as 4.1 ships it)
touch -d @0 "$art/init"
"$art/gen_init_cpio" -t 0 "$art/initramfs.list" | gzip -n -9 > "$art/initramfs.cpio.gz"
echo 'Smoke initramfs: no storage access, watchdog-reset return. Not an Android image.' > "$art/SCOPE.txt"
( cd "$art" && sha256sum init initramfs.cpio.gz > SHA256SUMS && cat SHA256SUMS )
