#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# KOA2 self-use KUAL installer with model/kernel/SHA256 gates and previous-version
# preservation. Operates strictly inside /mnt/us/readom-koa2 (never writes raw partitions).
set -u
BASE=${KOA2_INSTALL_BASE:-/mnt/us/readom-koa2}
SRC=${1:-}
[ -n "$SRC" ] && [ -d "$SRC" ] || { echo 'result=refused_missing_source'; exit 0; }
[ "$(uname -r)" = 4.1.15-lab126 ] || { echo 'result=refused_unknown_kernel'; exit 0; }
[ -f "$SRC/SHA256SUMS" ] && [ -f "$SRC/manifest.json" ] || { echo 'result=refused_missing_manifest'; exit 0; }
grep -q '"model": "KOA2"' "$SRC/manifest.json" || { echo 'result=refused_wrong_model'; exit 0; }
(cd "$SRC" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || { echo 'result=refused_checksum_mismatch'; exit 0; }

mkdir -p "$BASE" || { echo 'result=install_failed'; exit 0; }
STAGE="$BASE/.staging.$$"
rm -rf "$STAGE"
cp -r "$SRC" "$STAGE" || { rm -rf "$STAGE"; echo 'result=install_failed'; exit 0; }
(cd "$STAGE" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || {
    rm -rf "$STAGE"
    echo 'result=install_stage_verify_failed'
    exit 0
}

if [ -d "$BASE/current" ]; then
    rm -rf "$BASE/prev"
    mv "$BASE/current" "$BASE/prev" || { rm -rf "$STAGE"; echo 'result=install_backup_failed'; exit 0; }
    mv "$STAGE" "$BASE/current" || { mv "$BASE/prev" "$BASE/current"; echo 'result=install_swap_failed'; exit 0; }
    echo 'result=upgrade_ok'
else
    mv "$STAGE" "$BASE/current" || { rm -rf "$STAGE"; echo 'result=install_swap_failed'; exit 0; }
    echo 'result=install_ok'
fi
