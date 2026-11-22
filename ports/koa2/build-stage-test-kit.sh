#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Assembles the KUAL "Readom KOA2 Stage Test" extension in ~/artifacts, ready
# to copy to the Kindle's extensions/ folder. Host only; touches no device.
# It rebuilds the staging module and its client from this checkout, and pins
# the smoke zImage and initramfs it carries by SHA256 (the script on the device
# checks them before loading anything).
# The module is compiled against the module-abi tree, whose CRCs match stock on
# every symbol it imports (ports/koa2/stock-module-abi.md); the payload it
# carries is the headless-smoke kernel, which is the one that would boot.
# usage: build-stage-test-kit.sh [DEST]   (KOA2_STOCK_MODULES as in build-handoff-module.sh)
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
dest=${1:-$HOME/artifacts/koa2-stage-test-kit/readom-koa2-stage-test}
src=$HOME/readom-work/linux-headless-smoke out=$HOME/readom-work/out-module-abi
payload_out=$HOME/readom-work/out-headless-smoke
module=$HOME/artifacts/koa2-handoff-module
initramfs=$HOME/artifacts/smoke-initramfs/initramfs.cpio.gz
bash "$here/ports/koa2/build-handoff-module.sh" "$src" "$out" "$payload_out" "$module"
cflags='-std=gnu11 -O2 -Wall -Wextra -Werror -static'
arm-linux-gnueabi-gcc $cflags -I"$here/boot/koa2" -I"$here/boot/handoff" \
    "$here/boot/koa2/stage-client.c" "$here/boot/handoff/sha256.c" -o "$module/koa2-stage-client"
[[ -f $initramfs ]] || { echo "missing $initramfs (ports/koa2/build-smoke-initramfs.sh)" >&2; exit 1; }
rm -rf "$dest"; mkdir -p "$dest"
cp "$here"/ports/koa2/stage-test/{config.xml,menu.json,stage-test.sh,quiesce-dryrun.sh,marker-test.sh} "$dest/"
cp "$module/koa2_handoff_stage.ko" "$module/koa2-stage-client" "$dest/"
cp "$payload_out/arch/arm/boot/zImage" "$initramfs" "$dest/"
chmod 755 "$dest/stage-test.sh" "$dest/quiesce-dryrun.sh" "$dest/marker-test.sh" "$dest/koa2-stage-client"
(cd "$dest" && sha256sum koa2_handoff_stage.ko koa2-stage-client zImage initramfs.cpio.gz stage-test.sh quiesce-dryrun.sh marker-test.sh > SHA256SUMS)
# The module must have been built for exactly this zImage.
want=$(grep -o 'K2S_KERNEL_BYTES [0-9]*' "$module/target-lock.h" | cut -d' ' -f2)
[[ $(stat -c%s "$dest/zImage") == "$want" ]] || { echo 'zImage does not match the module lock' >&2; exit 2; }
echo "kit: $dest"
cat "$dest/SHA256SUMS"
