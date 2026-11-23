# 换电脑继续 KOA2 移植

截至 2026-11-21，Oasis 2 的暂存检查已在原厂系统通过；**现在可以不接 Kindle 继续主机侧开发**。
当前没有可用的 KOA2 Android 安装包，也没有在实机启动过新内核。
English: [CONTINUE.en.md](CONTINUE.en.md)。

## 1. 获取代码并确认位置

在 WSL 的 Linux 文件系统中执行，已有仓库先检查未提交修改：

```bash
cd ~/readom-work/readom
git status --short --branch
git switch port/koa2
git pull --ff-only origin port/koa2
```

首次获取时，在 WSL 中从 Keith-Bao/readom 克隆并选择 port/koa2 分支。
如果 status 显示未提交修改或 pull 拒绝，先保存并检查差异，不 reset、不强行覆盖。
仓库目录位置是约定；不同电脑可自行调整。
先看[当前状态](STATUS.md)，再看[交接实现](../../boot/koa2/README.md)和[静止顺序审计](handoff-quiesce.md)。

## 2. 已验证到哪里

| 验证层次 | 已有证据 | 仍不能证明 |
|---|---|---|
| 主机测试 | 契约、布局、FDT、复制表、静止顺序和失败回滚 | 原厂外设确实停止或恢复 |
| 裸机 QEMU | Cortex-A7 上跳转蹦床按约定执行 | i.MX7D 的真实外设和整机启动 |
| Kindle 暂存 | 模块加载、封存、卸载成功，plan_valid=1 | 复制载荷、跳转、新内核或 Android 启动 |

[实机报告](reports/2026-11-21-stage-test/report.txt)记录 result=sealed_plan_valid，
execute_available=0，insmod_exit/client_exit/rmmod_exit 均为 0。
报告附带的 [SHA256SUMS](reports/2026-11-21-stage-test/SHA256SUMS) 标识本次套件，重新构建后哈希可能变化。

## 3. 不接设备先做什么

1. USB 单项重载及 Windows 读盘已验证。触摸优先验证原厂休眠和唤醒能否保留输入节点与 Xorg 句柄，并以醒来后的触摸事件和界面响应为准。交接所需的 I²C 静止仍须独立验证。见[触摸休眠审计](handoff-quiesce.md)。
2. 继续调查 [EPDC 源码缺口](epdc-source-gap.md)。真机空跑时 EPDC 驱动仍被占用，目前没有证据表明它可以在此阶段释放 DMA 帧缓冲。
3. 继续保留暂存和执行的独立入口。交接验证条件见 [recovery.md](recovery.md) 第五节。
4. 改动后运行以下主机测试，分别记录通过、失败和跳过。

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

主机准备见[构建环境](build-environment.md)。源代码、交叉工具链、内核构建输出和 WSL 虚拟磁盘不会随 Git 拉取自动出现。

## 4. 何时才需要 Kindle

2026-11-23 的重启标记测试通过。旧版静止空跑时 EPDC 保持占用，USB 存储重载失败，触摸在强制重启后才恢复。新版 KUAL 入口为只读预检，已用它记录正常启动时的状态。

当前无需再操作 Kindle。恢复路径和主机测试就绪后，再按设备分别安排验证。空跑与新内核交接暂缓。设备入口见[实机操作手册](device-runbook.md)。

### 下一次真机操作
主机模拟和 ARM 编译已通过。首轮两分钟锁屏未观测到系统休眠。新脚本还读取现有挂起计数和筛选后的电源日志，醒后独立记录触摸。需要重装时用 `ports/koa2/build-stage-test-kit.sh` 在 WSL 构建套件，复制到 Kindle 的 `extensions/readom-koa2-stage-test`，并在设备盘校验 `SHA256SUMS`。
安全弹出并拔线，在 KUAL 选择 **Stock sleep: record before**；正常锁屏并保持拔线约 30 分钟，唤醒后选择 **Stock sleep: check after (tap screen)**，十秒内点屏并确认原厂界面有响应，再接回电脑取同一份报告。预期结果是 `stock_sleep_touch_observed`。它验证正常休眠前后输入节点及读者快照一致、醒后有触摸事件。交接所需的 I²C 静止仍待独立验证，继续暂停整机空跑和内核跳转。

## 5. 资料已经上传到哪里

旧版套件和当时的操作单存档在[2026-11-23 资料目录](materials/2026-11-23/README.md)：
十文件 KUAL 套件、模块导入表与 CRC 结果，以及标记测试和静止空跑的逐步操作单。
按用户要求，[资料文件夹](materials/2026-11-21/README.md)已直接加入本分支：原厂模块、去标识完整 dmesg、工具链、测试包、内核源码分卷和参考配置都可随 Git 拉取。
目录说明包含 SHA256 校验、源码合并和重建命令。
唯一缺少的是原厂运行时 DTB；未找到独立备份，构建 DTB 不能替代它。
本地原始日志仍保留；GitHub 中日志只删除设备和网络标识，其余时序保留。
