# 当前执行状态

English: [STATUS.en.md](STATUS.en.md)。流程见 [device-runbook.md](device-runbook.md)，
路线见 [ROADMAP.md](ROADMAP.md)。

## 一句话

2026-11-21，实机暂存测试通过（sealed_plan_valid），模块成功加载并卸载。
布局、FDT 改写和复制表审计通过；尚未启动新内核。主机 59 项通过为此前记录，本轮未重跑。

## 眼下的位置

| 问题 | 答案 |
|---|---|
| 下一步是什么 | 暂存测试已通过；停止设备推进，先实现静止顺序硬件 run/undo 与当次 EPDC 帧缓冲排除 |
| 实机结果 | `sealed_plan_valid`；insmod_exit=0、client_exit=0、rmmod_exit=0 |
| 那之后做什么 | 已收回 63 个原厂模块；本次加载成功，无需因预期的 CRC 不匹配而重试 |
| 什么在挡着 Android | EPDC 源码缺口（[epdc-source-gap.md](epdc-source-gap.md)），原厂内核树编不出来 |
| 有没有不可逆的操作 | 没有。本次已加载并卸载暂存模块；未执行跳转或分区写入 |

## 交接各件的状态

| 件 | 代码 | 测试 | 设备 |
|---|---|---|---|
| DTB/内存契约 | `boot/koa2/contract.c` | `koa2-contract` | 只读校验器跑过 |
| 布局校验 | `boot/koa2/plan.c` | `koa2-plan` | 实机暂存检查通过 |
| FDT 改写 | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | 实机暂存检查通过 |
| 复制表与审计 | `boot/koa2/copylist.c` | `koa2-copylist` | 实机暂存检查通过 |
| 暂存模块（无跳转） | `boot/koa2/stage.c` | `koa2-stage` | 加载、封存、卸载成功 |
| 原厂 CRC 核对 | `ports/koa2/module-crcs.py` | `koa2-module-crcs` | 已回收 63 个；离线核对尚未运行 |
| 跳转蹦床 | `boot/koa2/jump.S` | `koa2-jump-baremetal` | 未编进模块，未执行 |
| 静止顺序 | `boot/koa2/quiesce.c` | `koa2-quiesce` | 只有顺序，缺硬件实现 |
| 构建可复现性 | `ports/koa2/compare-kernel-build.py` | `koa2-compare-build` | 不适用 |

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
