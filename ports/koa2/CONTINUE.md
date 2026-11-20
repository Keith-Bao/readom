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

1. 按 boot/koa2/quiesce.h 的顺序，审计并实现硬件 run/undo；为每一步写清入口、完成条件、超时和失败恢复。当前 quiesce.c 只是顺序与回滚框架。
2. 设计当次读取 EPDC 动态帧缓冲地址并加入排除区的路径。历史地址不能当固定值；真实取值和停机效果留待下次上机验证。
3. 补充对应的主机失败路径测试，运行下面的 KOA2 测试；记录通过、失败和跳过，不能把缺依赖的跳过算通过。
4. 继续调查 [EPDC 源码缺口](epdc-source-gap.md)。这是完整 Android 的独立阻塞，不会因暂存检查通过自动解决。

```bash
python3 tools/run-tests.py --filter koa2
python3 tools/tests/test_doc_pairs.py
python3 tools/tests/test_doc_links.py
```

主机准备见[构建环境](build-environment.md)。源代码、交叉工具链、内核构建输出和 WSL 虚拟磁盘不会随 Git 拉取自动出现。
不要把暂存模块直接改成可跳转模块；保留分开的入口和验证门槛。

## 4. 何时才需要 Kindle

硬件 run/undo 和当次排除区实现完成、主机检查通过后，再制定一次范围明确的实机测试。
真正交接前必须能说明恢复路径、日志获取方式和每一步的停止条件。
测试由用户手动发起；当前不需要重复硬件采集或暂存测试。
设备流程在[实机操作手册](device-runbook.md)，阶段门槛在[路线图](ROADMAP.md)。

## 5. 哪些材料没有上传

GitHub 有源码、状态、检查过的精简实机报告和套件哈希；没有原厂模块、完整 dmesg、运行时 DTB、工具链或测试包二进制。
家用电脑保留 63 个原厂模块，离线 CRC 核对尚未做；没有这些模块不影响先写硬件实现和主机测试。
原始证据位置见[本次测试说明](reports/2026-11-21-stage-test/README.md)。
若后续工作确实依赖这些材料，再单独安排转移，不把设备原始数据加入仓库。
