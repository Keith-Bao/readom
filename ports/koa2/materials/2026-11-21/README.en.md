# KOA2 cross-computer materials (2026-11-21)

These files are committed directly to port/koa2 at the user's request. Pull the branch; no Release download is needed.
Chinese: [README.md](README.md).

## Contents and limits

| Directory | Contents |
|---|---|
| device-evidence | 63 stock modules, report, module list and all three dmesg snapshots; serial, MAC, LAN and email identifiers replaced, originals retained at home |
| stage-test-kit | The eight-file KUAL staging kit that passed on hardware, including its original SHA256SUMS |
| handoff-module | Module, target-lock.h, imports, version and hashes |
| toolchain | The AOSP GCC 4.9 archive used for this build |
| kernel-source | Verified stock Linux source archive split into parts of at most 48 MiB |
| kernel-reference | Module.symvers, configuration, System.map, compiler version and built DTB |

**Missing: no separate backup of the stock runtime DTB was found. It is not included.**
kernel-reference/imx7d-zelda.dtb is a build artifact, not /sys/firmware/fdt.
Export the runtime DTB read-only from a running device if later work requires it. Host tests do not currently require it.
The WSL disk, full kernel output tree and operating system are not included; dependencies are in [the build environment](../../build-environment.md).

## Restore on another computer

Clone or update port/koa2 inside WSL's Linux filesystem, then run:

```bash
set -e
cd ~/readom-work/readom/ports/koa2/materials/2026-11-21
sha256sum -c SHA256SUMS
mkdir -p ~/toolchains/aosp-arm-gcc49 ~/readom-work ~/koa2-evidence/stage-test
tar -xzf toolchain/aosp-arm-gcc49.tar.gz -C ~/toolchains/aosp-arm-gcc49
cat kernel-source/linux-4.1.15.tar.gz.part* > ~/readom-work/linux-4.1.15.tar.gz
printf '%s  %s
' efb1b966fedcba318af381080f6a699642a66a929c42e8578aeb6e3fe240ea93 ~/readom-work/linux-4.1.15.tar.gz | sha256sum -c -
cp -a device-evidence ~/koa2-evidence/stage-test/20261120-160050-26549
export KOA2_TC_ARCHIVE="$PWD/toolchain/aosp-arm-gcc49.tar.gz"
cd ~/readom-work/readom
bash ports/koa2/build-stock-kernel.sh headless-smoke
```

These commands assume a new environment. Preserve any existing evidence directory first to avoid nesting copies.
Rebuild Linux; the reference files are not a complete build output.
Install toolchain dependencies before building. The source archive hash is fixed; a rebuilt zImage can differ due to timestamps.
The kit is staging-only, for a jailbroken Oasis 2 on firmware 5.16.2.1.1. It is not Android and cannot jump.
No device retest is needed now; see [continuation](../../CONTINUE.en.md).

## Verification and originals

SHA256SUMS covers every file here except itself; stage-test-kit/SHA256SUMS separately verifies the exact tested payloads.
redactions.json records replacement counts; other log lines remain, and modules and payload binaries are unchanged.
Only de-identified log copies are uploaded. The local originals were not modified.
