#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Roll back KOA2 self-use installation from prev/ to current/ with SHA256 verification.
set -u
BASE=${KOA2_INSTALL_BASE:-/mnt/us/readom-koa2}
[ "$(uname -r)" = 4.1.15-lab126 ] || { echo 'result=refused_unknown_kernel'; exit 0; }
[ -d "$BASE/prev" ] && [ -f "$BASE/prev/SHA256SUMS" ] || { echo 'result=refused_no_previous_version'; exit 0; }
(cd "$BASE/prev" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || { echo 'result=refused_prev_corrupted'; exit 0; }
rm -rf "$BASE/failed"
[ -d "$BASE/current" ] && mv "$BASE/current" "$BASE/failed"
mv "$BASE/prev" "$BASE/current" || { echo 'result=rollback_failed'; exit 0; }
(cd "$BASE/current" && sha256sum -c SHA256SUMS >/dev/null 2>&1) || { echo 'result=rollback_verify_failed'; exit 0; }
echo 'result=rollback_ok'
