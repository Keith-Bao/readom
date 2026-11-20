# KOA2 跨电脑资料包（2026-11-21）

本目录按用户要求直接保存在 Git 分支 port/koa2；拉取分支即可获得，无需下载 Release。
English: [README.en.md](README.en.md)。

## 内容与边界

| 目录 | 内容 |
|---|---|
| device-evidence | 本次 63 个原厂 .ko、报告、模块清单、三份完整时序 dmesg；日志中的序列号、MAC、局域网地址和邮件标识已替换，原件留家用电脑 |
| stage-test-kit | 本次实机通过的 8 文件 KUAL 暂存套件，包含原始 SHA256SUMS |
| handoff-module | 配套模块、target-lock.h、导入表、版本和哈希 |
| toolchain | 已使用的 AOSP GCC 4.9 归档 |
| kernel-source | 已验证的原厂 Linux 源码归档分卷，每卷最大 48 MiB |
| kernel-reference | 本次构建树的 Module.symvers、配置、System.map、编译器版本及构建 DTB |

**缺失：原厂运行时 DTB 没有找到独立备份，本目录不含该文件。**
kernel-reference/imx7d-zelda.dtb 是构建产物，不能冒充 /sys/firmware/fdt。
如后续确实需要运行时 DTB，须在设备运行时只读导出；目前写主机测试不依赖它。
没有上传 WSL 虚拟磁盘、整个内核输出树或整个操作系统；依赖安装仍见[构建环境](../../build-environment.md)。

## 在公司电脑恢复

从 WSL 的 Linux 文件系统克隆或更新 port/koa2，再执行：

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

以上用于新环境。已有同名证据目录时先保留旧内容，不要嵌套覆盖复制。
Linux 内核构建需要重新执行；仓库里的参考文件不等于完整构建输出。
重建前检查工具链依赖；源码归档哈希固定，zImage 可能因构建时间戳而变化。
已有测试包只供已越狱 Oasis 2 / 固件 5.16.2.1.1 暂存检查，不是 Android 安装包，且不含跳转功能。
当前无需重跑设备测试；后续任务见[换电脑继续](../../CONTINUE.md)。

## 校验与原始证据

SHA256SUMS 覆盖本目录除校验表自身外的全部文件；stage-test-kit/SHA256SUMS 额外核对本次实际运行的载荷。
redactions.json 记录日志替换次数；日志其余行保留，二进制模块和测试载荷未改写。
GitHub 上传的是去标识副本，本地原始报告与完整日志未被修改。
