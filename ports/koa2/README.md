# Oasis 2 本地移植工作区

Oasis 2（固件 5.16.2.1.1，内核 4.1.15-lab126）保留原厂系统、另外启动 Android 8.1 的
移植工作区。English: [README.en.md](README.en.md)。

**现在到哪了**：暂存与校验代码已有主机测试；**实机暂存测试已通过（2026-11-21）**，
没有启动过新内核。路线见 [ROADMAP.md](ROADMAP.md)，逐条进度见 [STATUS.md](STATUS.md)。

**不能用 PW3 的安装包**：那是 3.0.35 内核、3G/1G 划分的另一款机器。

**暂时不需要 Kindle**：在公司电脑继续请先读[换电脑继续](CONTINUE.md)。实机暂存已通过，硬件静止和真正的新内核启动仍未完成。

## 要上手的话从哪读

| 你想做的事 | 读这个 |
|---|---|
| 在设备上跑这次的测试 | [device-runbook.md](device-runbook.md)，自带前提、命令和出错处理 |
| 搞清楚交接怎么设计的 | [boot/koa2/README.md](../../boot/koa2/README.md) |
| 配置构建环境 | [build-environment.md](build-environment.md) |
| 看这台设备实测了什么 | [boot-preflight-findings.md](boot-preflight-findings.md)、[hardware-findings.md](hardware-findings.md) |
| 知道为什么还不能启动 Android | [epdc-source-gap.md](epdc-source-gap.md)、[android41-audit.md](android41-audit.md) |

## 这台设备已确认的事实

- 内核 `4.1.15-lab126`，`#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023`，armv7l，双核在线。
- 2G/2G 划分，`PAGE_OFFSET` 与 `PHYS_OFFSET` 都是 0x80000000，512 MiB 全是 lowmem 且恒等映射。
- 可用 RAM 两段：0x80000000–0x9bffffff、0x9c500000–0x9fffffff；中间的空洞是 EPDC 的 no-map 保留区。
- 运行时 FDT 有两条 memreserve（原厂 FDT 自身、原厂 initrd），`/memory` 是 0x80000000 + 0x20000000。
- eMMC 走 Falcon BIOS（`falcon_blk`）；源码上可以改回标准 `sdhci-esdhc-imx`，未上机验证。
- 看门狗 60 秒、WDOG_B 复位，`imx2_wdt` 在原厂配置下由内核定时器喂狗直到 `/dev/watchdog` 被打开。
- 原厂每次经重启路径都会把 printk 写到 eMMC 扇区 0x9a400 起 256 KiB。
- USB 存储卷约 6.67 GB；`system/version.txt` 为 Kindle 5.16.2.1.1 (409745 002)。
- 根目录 `VERSION` 是越狱组件版本，不是固件版本。

## 这个目录里有什么

| 文件 | 作用 |
|---|---|
| `device-runbook.md` | 设备测试流程（本次要做的事） |
| `build-stage-test-kit.sh` | 造设备测试套件 |
| `build-handoff-module.sh` | 编交接暂存模块，并在导出前自查 |
| `build-stock-kernel.sh` | 编原厂/实验内核 |
| `derive-kernel-footprint.py` | 量出新内核解压后占用的物理范围 |
| `module-crcs.py` | 判断模块能否通过原厂内核的版本检查 |
| `compare-kernel-build.py` | 判断两次构建是不是同一个内核 |
| `stage-test/`、`preflight/`、`kual/` | 三个 KUAL 扩展（暂存测试、只读采集、硬件采集） |
| `smoke-init.c`、`build-smoke-initramfs.sh` | 最小 initramfs 的 PID 1 与打包 |

交接的代码在 [boot/koa2/](../../boot/koa2/)。主机测试一条命令全跑：

```bash
python3 tools/run-tests.py --filter koa2
```

## 安全约束（一直有效）

- 不写分区、不改引导、不改 IDME、不动校准数据。
- 设备实验只由用户发起，并且只在能说明怎么恢复之后。
- 原始设备数据（运行时 DTB、bootargs、内核日志、原厂模块）只留在设备和
  `~/koa2-evidence/`，不进仓库。
- 不绕过公司策略和 DLP：仓库文件只在 WSL 的 ext4 里改和构建。

reports/ 下公开的是检查过的精简暂存报告与套件哈希；完整原始设备证据仍留本地。

用户授权上传的模块、去标识日志与构建材料见[资料文件夹](materials/2026-11-21/README.md)；此前仅存本地的限制由此次明确授权更新。
