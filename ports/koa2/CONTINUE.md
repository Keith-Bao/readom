# 换电脑继续 KOA2 移植

截至 2026-11-21，Oasis 2 的暂存检查已在原厂系统通过；**现在可以不接 Kindle 继续主机侧开发**。
当前没有可用的 KOA2 Android 安装包，也没有在实机启动过新内核。
English: [CONTINUE.en.md](CONTINUE.en.md)。

## 1. 获取代码并确认位置

在 WSL 的 Linux 文件系统中执行，已有仓库先检查未提交修改：

```bash
cd ~/readom-work/readom
git status --short --branch
git fetch origin
git switch port/koa2-quiesce-preflight
git pull --ff-only
```

首次获取时，在 WSL 中从 Keith-Bao/readom 克隆并选择 port/koa2-quiesce-preflight 分支。
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

1. **触摸 I²C**：主机侧已完成，见[触摸单项静止设计](handoff-quiesce.md)，待上机运行 **Touch only**（[操作单](device-runbook.md)）。原任务：用已校验的原厂源码追踪 `cyttsp5_core_sleep_()`、IRQ 处理、watchdog 和 startup 工作队列。设计能排空在途 IRQ/I²C、独立确认停机并可唤醒的方案，保留 evdev 节点和 Xorg。补齐断电失败、超时和回滚测试。不能用 `sleep_status=off` 或普通休眠结果放宽 `quiesce-verify.c`。
2. **EPDC**：引用来源的只读采集已加入（**Inspect EPDC framebuffer users**，见[静止审计](handoff-quiesce.md)），待上机。原任务：对照 [EPDC 源码缺口](epdc-source-gap.md) 和原厂日志设计只读采集，定位 framebuffer 打开者、驱动引用和待完成刷新。此前模块有 3 个引用，先论证如何释放引用和处置 DMA 帧缓冲，再设计单项停用。
3. **交接验证**：保持暂存和执行入口分开。按 [recovery.md](recovery.md) 检查回退和早期日志条件。QEMU 裸机跳转测试 2026-11-24 已排查：本机复现不出“缺串口输出”，直接运行和 8 核满载下 6 个用例都通过。原脚本固定等 2.5 秒才读串口，主机忙时会误判为没有输出，已改为轮询到结果（上限 20 秒），失败时附带 QEMU 状态和输出。破坏寄存器约定后测试仍报 FAIL。2026-11-24 已完成回退与早期日志源码复核（见 [recovery.md](recovery.md)）：`printk_reserve_buf()`、`imx7d_reserve()`、`dump_lastk_to_mmc()` 空操作和 `pstore/ramoops` 映射均已核实，并已为实验内核补齐 `log_goal_first`（`0x9be00000`）的内/外 D-cache 回写（`prepare-headless-source.py`、`koa2-printk-writeback` 及 `out-headless-smoke` 重建验证通过）；2026-11-24 真机套件不启动新内核，保持原封不动。
4. 按下列命令跑主机测试，分别记录通过、失败和跳过。

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

主机准备见[构建环境](build-environment.md)。源代码、交叉工具链、内核构建输出和 WSL 虚拟磁盘不会随 Git 拉取自动出现。

## 4. 何时才需要 Kindle

已确认：暂存通过，标记跨正常重启保存，USB 单项重载后 Windows 能读盘。2026-11-24 原厂休眠报告为 `stock_sleep_touch_observed`，记录 2 次成功挂起、0 次失败和 3,602,010 ms 累计休眠增量。醒后触摸节点与 Xorg 句柄标记一致，探针收到完整接触事件。原始报告已在家里电脑校验留存，设备上的测试入口和副本已清理。详见[真机结果及审计](handoff-quiesce.md)。

**公司阶段可以不接 Kindle。** 等主机侧的触摸和 EPDC 方案、模拟测试和回滚流程就绪，再准备一次真机批次。优先做只读采集，然后按证据安排单项测试。每项报告取回并校验后，从设备清理对应入口和副本。整机空跑、新内核跳转暂缓。

## 5. 资料已经上传到哪里

**下一批真机测试**：[2026-11-24 套件与操作单](materials/2026-11-24/README.md)（EPDC 占用、Wi-Fi 占用与运行时 DTB 只读备份、CPU1 单项下线/上线、触摸单项休眠与唤醒，共 5 项），已用含 printk 缓存回写的最新内核重新构建并校验（12 项 SHA256），拷到 Kindle 即可。

旧版套件和当时的操作单存档在[2026-11-23 资料目录](materials/2026-11-23/README.md)：
十文件 KUAL 套件、模块导入表与 CRC 结果，以及标记测试和静止空跑的逐步操作单。
按用户要求，[资料文件夹](materials/2026-11-21/README.md)已直接加入本分支：原厂模块、去标识完整 dmesg、工具链、测试包、内核源码分卷和参考配置都可随 Git 拉取。
目录说明包含 SHA256 校验、源码合并和重建命令。
唯一缺少的是原厂运行时 DTB；未找到独立备份，构建 DTB 不能替代它。
本地原始日志仍保留；GitHub 中日志只删除设备和网络标识，其余时序保留。
