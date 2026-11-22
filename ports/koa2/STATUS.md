# 当前执行状态

English: [STATUS.en.md](STATUS.en.md)。流程见 [device-runbook.md](device-runbook.md)，
路线见 [ROADMAP.md](ROADMAP.md)。

## 一句话

2026-11-21，实机暂存测试通过（sealed_plan_valid），模块成功加载并卸载。
布局、FDT 改写和复制表审计通过；尚未启动新内核。
2026-11-22 跳转标记的写入已接上模块（`K2S_MARK`），不是跳转，也没打开跳转。
主机 65 项通过、0 项失败。

## 眼下的位置

| 问题 | 答案 |
|---|---|
| 下一步是什么 | 设备上两件事，都不碰跳转：放一个标记、重启、确认它还在（`marker-test.sh`，顺带验证复位是否真的保留 DRAM），以及静止空跑（`quiesce-dryrun.sh`，停设备再全部放回） |
| 实机结果 | `sealed_plan_valid`；insmod_exit=0、client_exit=0、rmmod_exit=0 |
| 那之后做什么 | 原厂模块已核对完（[stock-module-abi.md](stock-module-abi.md)）：12 个符号与构建树不一致，模块一个都没用到 |
| 什么在挡着 Android | EPDC 源码缺口（[epdc-source-gap.md](epdc-source-gap.md)），原厂内核树编不出来 |
| 有没有不可逆的操作 | 没有。本次已加载并卸载暂存模块；未执行跳转或分区写入 |
| 跳转失败能回来吗 | 源码分析见 [recovery.md](recovery.md)：看门狗 60 秒兜底，日志经 printk 缓冲跨复位；其中两项还没满足 |

## 交接各件的状态

| 件 | 代码 | 测试 | 设备 |
|---|---|---|---|
| DTB/内存契约 | `boot/koa2/contract.c` | `koa2-contract` | 只读校验器跑过 |
| 布局校验 | `boot/koa2/plan.c` | `koa2-plan` | 实机暂存检查通过 |
| FDT 改写 | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | 实机暂存检查通过 |
| 复制表与审计 | `boot/koa2/copylist.c` | `koa2-copylist` | 实机暂存检查通过 |
| 暂存模块（无跳转） | `boot/koa2/stage.c` | `koa2-stage` | 加载、封存、卸载成功 |
| 原厂 CRC 核对 | `ports/koa2/module-crcs.py` | `koa2-module-crcs`、`koa2-stock-abi` | 63 个原厂模块已离线核对：31 个导入全部一致 |
| 跳转前标记 | `boot/koa2/marker.c`、模块侧 `K2S_MARK` | `koa2-marker`、`koa2-marker-wiring`、`koa2-stage` | 35 个导入与原厂 CRC 全部一致；未在设备上放过标记 |
| 跳转蹦床 | `boot/koa2/jump.S` | `koa2-jump-baremetal` | 未编进模块，未执行 |
| 静止顺序 | `boot/koa2/quiesce.c`、`quiesce-verify.c` | `koa2-quiesce`、`koa2-quiesce-verify`、`koa2-quiesce-dryrun` | 停设备改由用户空间做（见 [handoff-quiesce.md](handoff-quiesce.md) 第六节）；空跑脚本已在套件里，未在设备上跑过 |
| 构建可复现性 | `ports/koa2/compare-kernel-build.py` | `koa2-compare-build` | 不适用 |

## 模块 ABI（2026-11-21 查明）

`insmod` 成功的原因已查清，见 [stock-module-abi.md](stock-module-abi.md)：modversions 跳过
release 字符串，而模块导入的 31 个符号 CRC 恰好与原厂一致。构建树与原厂在 1129 个共同符号上
有 12 个不一致——9 个是 framebuffer（面板关着，受 EPDC 缺口所限），3 个是
`CONFIG_FALCON_WRAPPER` 让 `kernel/sched/core.c` 多 include 一个头文件造成的。
现在模块针对新的 `out-module-abi` 树编译，那 3 个已与原厂一致；载荷仍是 `out-headless-smoke`。
`cpu_down` 没有原厂模块导入，核对不了，只能靠设备上 `insmod` 检验。

## 当前产物

| 产物 | 值 |
|---|---|
| 内核 release | `4.1.15-lab126-koa2-headless-smoke`，97 个模块 vermagic 匹配 |
| Image（build-id 清零） | `e2eba61b8420…`，换输出目录重建后相同 |
| zImage | `4c69f47e…`，每次构建都会变（lzop 时间戳），钉在 target-lock.h |
| DTB | `fbbe9517…` |
| 新内核占用 | 页目录 0x80004000，解压到 0x80008000，_end 0x80c55a0c |
| 交接布局 | zImage 0x80d00000、FDT 0x81400000、initramfs 0x81500000 |

## 更早的记录

逐日的开发记录移到了 [worklog.md](worklog.md)。

[本次实机证据与换电脑继续的说明](reports/2026-11-21-stage-test/README.md)。

[换电脑继续](CONTINUE.md)：接下来的主机工作暂时不需要设备。
