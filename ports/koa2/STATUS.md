# 当前执行状态

English: [STATUS.en.md](STATUS.en.md)。流程见 [device-runbook.md](device-runbook.md)，
路线见 [ROADMAP.md](ROADMAP.md)。

## 一句话

交接的主机侧工作全部完成并有测试（主机 59 项通过、0 项失败）；**设备上一步都没跑过**，
没有启动过新内核，也没有在原厂内核里加载过任何模块。

## 眼下的位置

| 问题 | 答案 |
|---|---|
| 下一步是什么 | 用户回家后按 [device-runbook.md](device-runbook.md) 跑一次暂存测试 |
| 最可能的结果 | `insmod_refused`：模块对 headless-smoke 树编译，符号 CRC 与原厂对不上 |
| 那之后做什么 | 用设备拷回来的原厂 `.ko` 核对 CRC，换掉对不上的导入，重建再跑 |
| 什么在挡着 Android | EPDC 源码缺口（[epdc-source-gap.md](epdc-source-gap.md)），原厂内核树编不出来 |
| 有没有不可逆的操作 | 没有。至今所有设备操作都是只读，或只写 `/mnt/us` 下的报告目录 |

## 交接各件的状态

| 件 | 代码 | 测试 | 设备 |
|---|---|---|---|
| DTB/内存契约 | `boot/koa2/contract.c` | `koa2-contract` | 只读校验器跑过 |
| 布局校验 | `boot/koa2/plan.c` | `koa2-plan` | 未运行 |
| FDT 改写 | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | 未运行 |
| 复制表与审计 | `boot/koa2/copylist.c` | `koa2-copylist` | 未运行 |
| 暂存模块（无跳转） | `boot/koa2/stage.c` | `koa2-stage` | 未加载 |
| 原厂 CRC 核对 | `ports/koa2/module-crcs.py` | `koa2-module-crcs` | 缺原厂模块 |
| 跳转蹦床 | `boot/koa2/jump.S` | `koa2-jump-baremetal` | 未编进模块，未执行 |
| 静止顺序 | `boot/koa2/quiesce.c` | `koa2-quiesce` | 只有顺序，缺硬件实现 |
| 构建可复现性 | `ports/koa2/compare-kernel-build.py` | `koa2-compare-build` | 不适用 |

## 当前产物

| 产物 | 值 |
|---|---|
| 内核 release | `4.1.15-lab126-koa2-headless-smoke`，97 个模块 vermagic 匹配 |
| Image（build-id 清零） | `e2eba61b8420…`，换输出目录重建后相同 |
| zImage | `4921c4b2…`，每次构建都会变（lzop 时间戳），钉在 target-lock.h |
| DTB | `fbbe9517…` |
| 新内核占用 | 页目录 0x80004000，解压到 0x80008000，_end 0x80c55a0c |
| 交接布局 | zImage 0x80d00000、FDT 0x81400000、initramfs 0x81500000 |

## 更早的记录

逐日的开发记录移到了 [worklog.md](worklog.md)。
