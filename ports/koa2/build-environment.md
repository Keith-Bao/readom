# 本机 Linux 构建环境

- WSL 3.0.5.0，Readom-KOA2，Ubuntu 22.04.5，x86_64。
- 虚拟磁盘位于 E:\Readom-KOA2，源代码和构建输出位于 ext4 内。
- 8 CPU、10GB 内存、8GB swap。Windows IPv6 禁用导致 mirrored 回退 NAT；
  已实测 Linux 可直接访问 GitHub 和 Ubuntu 源，不依赖 localhost Clash 代理。
- rootfs 来源 https://cloud-images.ubuntu.com/wsl/jammy/current/
  ubuntu-jammy-wsl-amd64-ubuntu22.04lts.rootfs.tar.gz
- rootfs SHA256：1483cc5c1dce13064f774834cbffdff226559fd522a67a381a8ea77d63fb4109。
- 普通用户 builder；Windows 执行 `wsl -d Readom-KOA2 -u builder -- bash -lc '<command>'`。
- Git 工作副本 /home/builder/readom-work/readom；内核源码 linux-stock、输出 out-stock；
  主机日志 /home/builder/artifacts，内核产物 /home/builder/artifacts/stock-kernel。

## 依赖

安装 build-essential gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi git
ca-certificates curl bison flex libssl-dev libncurses-dev u-boot-tools bc python3
xz-utils cpio rsync file device-tree-compiler qemu-system-arm。

## 工具链

上游锁定 Git revision：0390252b6bcc6217966ade31d07f8b12f6f78f89。
从 android.googlesource.com/platform/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9
的该 revision `+archive/<revision>.tar.gz` 下载。
下载归档 SHA256：0d273adc4ac4672fe67a99965bf4ce3814784433d0ac2d80689ec28d49d11c25。
这是 Gitiles 直接归档，与根 source-lock.json 中上游自行打包的归档哈希不同，
不能把两者视作同一压缩文件或跳过完整性检查。目标 revision 相同。
解压至 /home/builder/toolchains/aosp-arm-gcc49。
实测 arm-linux-androidkernel-gcc：4.9 20150123 (prerelease)。

## 兼容调整与验证

Linux 4.1 自带 dtc 的 yylloc 重复定义在 GCC 11 的 -fno-common 默认下链接失败。
KOA2 构建入口通过 make 命令行 HOSTCFLAGS=-O2\ -fcommon，仅调整主机工具。
ARM KCFLAGS=-fno-pic\ -fno-pie。未改动原厂驱动源码。
主机测试：run-tests.py --filter luma：2 passed / 0 failed。
完整内核构建状态以 STATUS.md 为准。
