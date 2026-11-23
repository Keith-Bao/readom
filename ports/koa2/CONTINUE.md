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

前两项已经做完，做法和原来的设想不同，原因见 [handoff-quiesce.md](handoff-quiesce.md) 第六节：

- **停设备不在模块里做**。能停设备的符号基本都核对不了（`lock_fb_info` 的 CRC 与原厂不一致，
  碰 framebuffer 就会被拒绝加载），而且时钟一 gate，读外设寄存器会挂总线——
  "读引擎状态确认空闲"恰恰会在静止成功时把设备挂死。改为用户空间走内核自己的卸载/下线路径
  （`stage-test/quiesce-dryrun.sh`），模块只验结果（`quiesce-verify.c`）。
- **EPDC 帧缓冲排除不必做了**。EPDC 的 remove 路径会释放那些 DMA 帧缓冲，卸载之后它们变回
  普通空闲页，没有需要排除的东西。

现在不接设备能做的：

1. ~~把跳转前的标记接到跳转阶段~~ **已完成（2026-11-22）**：写入在模块的 `K2S_MARK` 里
   （`koa2_marker_place`），不是跳转。接的时候 [recovery.md](recovery.md) 第四节原来写的
   三件事都要改：标记写在活动半区**末尾**、必须自己刷出缓存（看门狗复位不清缓存）、
   `__arm_ioremap` 对 RAM 会拒绝所以用 `phys_to_virt`。符号上有两个坑会让模块直接加载失败，
   `koa2-marker-wiring` 守着。设备上验证它活过复位要分两次做，见下面第四节。
2. 继续调查 [EPDC 源码缺口](epdc-source-gap.md)。这是完整 Android 的独立阻塞，
   不会因暂存检查通过自动解决。
   2026-11-23 进展：两块在这一份波形里的大小和位置已经从设备日志读出；5 个不能桩的函数里，
   3 个错了会伤硬件，其中包括值最好拿的 `mxc_set_vcom`；夜间模式（`GCK16`/`GLKW16`）由
   用户空间触发，第一版要在提交入口拒掉；`einkwf_panel_*` 没有调用者。下一步见该文件末尾。
3. 跳转阶段本身（`boot/koa2/jump.S` 已写并在 QEMU 裸机验证过，但没编进任何模块）——
   接入之前要先满足 [recovery.md](recovery.md) 第五节的全部条件。
4. 任何改动都跑下面的测试；记录通过、失败和跳过，不能把缺依赖的跳过算通过。

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

主机准备见[构建环境](build-environment.md)。源代码、交叉工具链、内核构建输出和 WSL 虚拟磁盘不会随 Git 拉取自动出现。
不要把暂存模块直接改成可跳转模块；保留分开的入口和验证门槛。

## 4. 何时才需要 Kindle

下一次上机有两件，顺序随意，都不碰跳转：

- **标记测试**（[device-runbook.md](device-runbook.md) 第 8c 节）：放一个标记、重启、
  再读 `/proc/printkbuf` 确认它还在。它同时验证 [recovery.md](recovery.md) 第二节那个
  只靠源码推断的结论——复位保留 DRAM。标记没活下来的话，那一节是错的。
- **静止空跑**（第 8b 节）：停设备再全部放回，不加载模块、不跳转。

真正交接前必须能说明恢复路径、日志获取方式和每一步的停止条件——
恢复路径的分析已经写了（[recovery.md](recovery.md)），其中设备上的两项还没做。
测试由用户手动发起；当前不需要重复硬件采集或暂存测试。
设备流程在[实机操作手册](device-runbook.md)，阶段门槛在[路线图](ROADMAP.md)。

## 5. 资料已经上传到哪里

今晚上机要用的套件和操作单在[2026-11-23 资料目录](materials/2026-11-23/README.md)：
十文件 KUAL 套件、模块导入表与 CRC 结果，以及标记测试和静止空跑的逐步操作单。
按用户要求，[资料文件夹](materials/2026-11-21/README.md)已直接加入本分支：原厂模块、去标识完整 dmesg、工具链、测试包、内核源码分卷和参考配置都可随 Git 拉取。
目录说明包含 SHA256 校验、源码合并和重建命令。
唯一缺少的是原厂运行时 DTB；未找到独立备份，构建 DTB 不能替代它。
本地原始日志仍保留；GitHub 中日志只删除设备和网络标识，其余时序保留。
