# 当前执行状态

English: [STATUS.en.md](STATUS.en.md)。流程见 [device-runbook.md](device-runbook.md)，
路线见 [ROADMAP.md](ROADMAP.md)。

## 一句话

2026-11-21，实机暂存测试通过（sealed_plan_valid），模块成功加载并卸载。
布局、FDT 改写和复制表审计通过；尚未启动新内核。
2026-11-22 跳转标记的写入已接上模块（`K2S_MARK`），不是跳转，也没打开跳转。
主机 26 项 KOA2 测试通过、0 项失败。
2026-11-23 读标记的脚本改用 `tr` 取文本并要求读取自证成功：原来用 `strings`，
这台机器没有该 applet 的证据，缺失时会误报成 `marker_absent`。
旧版套件和当时的操作单存档在[资料目录](materials/2026-11-23/README.md)。

2026-11-23 实机确认标记能跨正常重启；2026-11-24 原厂休眠验证通过（`stock_sleep_touch_observed`），USB 单项重载恢复读盘。同日完成回退与早期日志审计、实验内核 printk 缓冲 D-cache 回写、QEMU 双核 SELinux `policy30` enforcing + 多 context Binder 联测、统一 Android 4.1 内核完整构建、安全第一版 EPDC 辅助层（`epdc-v1`）ARM 编译与 `device/amazon/koa2/` 硬件契约；2026-11-24 真机套件已扩展至 5 项测试（12 项 SHA256），见 [2026-11-24 操作单](materials/2026-11-24/README.md)。

## 眼下的位置

| 问题 | 答案 |
|---|---|
| 下一步是什么 | **阶段 E 全部外设实机静止路径已齐备**：2026-11-27 实机 `epdc-quiesce.sh` v3 返回 `epdc_quiesce_restored`（3 次实机证实 `epdc_unloaded=1`/`epdc_reloaded=1`），`full-quiesce-dryrun.sh` v4（`usb,touch,cpu1`）在 3.58 秒内完整通过（`result=full_quiesce_dryrun_restored`）。阶段 E → F 已动工：`boot/koa2/jump.S` 已接入**单独的执行模块** `koa2_handoff_exec.ko`（源码与暂存模块同一套，exec.c 在 `KOA2_EXEC_BUILD` 下并入），新增只读空跑 `K2S_EXEC_DRYRUN`——校验静止、重审复制表、在自有页里装好蹦床并填参数、屏蔽 IRQ/FIQ 只读 CP15，然后停在关缓存前、全部放回，不跳转（`execute_available` 恒为 0）。主机 36 项全过，执行模块按 `build-exec-module.sh` 三道门禁编通（CP15 写仅在蹦床内、无跳转调用、CRC 核对 `passed: true`）。下一步上机跑 Exec dry-run，再准备首次带看门狗回退的真机跳转 |
| 实机结果 | 暂存 `sealed_plan_valid`；重启标记 `marker_survived`；USB 单项重载通过；原厂休眠 `stock_sleep_touch_observed`；CPU1 单项 `cpu1_quiesce_restored`；EPDC 引用 `matched`（`Xorg`/`awesome`/`blanket`）；运行时 DTB 已备份；触摸单项复测已证实断电（`disabled`）、I²C2 归零（`874→0→502`）与醒后触摸恢复（`after_touch=observed`，Xorg 句柄不变；`asleep_touch_irqs=-2` 系 CPU1 动态下线丢列） |
| 那之后做什么 | 原厂模块已核对完（[stock-module-abi.md](stock-module-abi.md)）：12 个符号与构建树不一致，模块一个都没用到 |
| 什么在挡着 Android | EPDC 源码缺口（[epdc-source-gap.md](epdc-source-gap.md)）：安全第一版（`epdc-v1`，拒夜间模式）已在统一 Android 4.1 内核完整编通并过 `modpost` 链接出 484 KB `mxc_epdc_v2_fb.ko`（`kernel_end=0x80c8a94c` 落入 `0x80d00000` 交接窗口），尚未上机；AOSP 8.1 设备树 `device/amazon/koa2/` 已接通镜像构建所需的 HAL/sepolicy：manifest 声明的每个 HAL 都在 `koa2.mk` 补齐 service 包（light/power/health/keymaster/graphics/audio/sensors），删去本树无 service 的 wifi.supplicant；`BOARD_HAL_STATIC_LIBRARIES := libhealthd.koa2`；`sepolicy/genfs_contexts` 按原厂 DTB 标注固定地址的 EPDC（`306f0000.epdc`），i2c 动态叶子节点留采集模板。`koa2-userdebug` 已在 WSL 以 30% 算力（`-j2`+`taskset`+`nice`）从零完整构建成功（`system.img` 实占 365 MiB < 512 MiB 预算，vendor manifest 装配 10 个 HAL 及其 `-impl`），镜像大小/SHA256 见 [worklog.md](worklog.md)；能否真机启动属下一次上机验证 |
| 有没有不可逆的操作 | 没有。本次已加载并卸载暂存模块；未执行跳转或分区写入 |
| 跳转失败能回来吗 | 见 [recovery.md](recovery.md)：printk 缓冲 D-cache 回写已补进实验内核；待真机静止可逆后再验证跨 `WDOG_B` 复位 |

## 交接各件的状态

| 件 | 代码 | 测试 | 设备 |
|---|---|---|---|
| DTB/内存契约 | `boot/koa2/contract.c` | `koa2-contract` | 只读校验器跑过 |
| 布局校验 | `boot/koa2/plan.c` | `koa2-plan` | 实机暂存检查通过 |
| FDT 改写 | `boot/koa2/fdt-rewrite.c` | `koa2-fdt-rewrite` | 实机暂存检查通过 |
| 复制表与审计 | `boot/koa2/copylist.c` | `koa2-copylist` | 实机暂存检查通过 |
| 暂存模块（无跳转） | `boot/koa2/stage.c` | `koa2-stage` | 加载、封存、卸载成功 |
| 原厂 CRC 核对 | `ports/koa2/module-crcs.py` | `koa2-module-crcs`、`koa2-stock-abi` | 63 个原厂模块已离线核对：31 个导入全部一致 |
| 跳转前标记 | `boot/koa2/marker.c`、模块侧 `K2S_MARK` | `koa2-marker`、`koa2-marker-wiring`、`koa2-stage` | 实机放置后跨正常重启读回 |
| 跳转蹦床 | `boot/koa2/jump.S` | `koa2-jump-baremetal`、`koa2-exec-wiring` | 已编进执行模块 `koa2_handoff_exec.ko`，由空跑装好但不执行 |
| 静止顺序 | `boot/koa2/quiesce.c`、`quiesce-verify.c` | `koa2-quiesce`、`koa2-quiesce-verify`、`koa2-quiesce-dryrun` | 旧版空跑失败，已用只读预检替换设备入口 |
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
| zImage | `d2a93246…`（2026-11-24 含 printk 缓存回写重建；每次构建都会变，钉在 target-lock.h） |
| DTB | `fbbe9517…` |
| 新内核占用 | 页目录 0x80004000，解压到 0x80008000，_end 0x80c55a0c |
| 交接布局 | zImage 0x80d00000、FDT 0x81400000、initramfs 0x81500000 |

## 更早的记录

逐日的开发记录移到了 [worklog.md](worklog.md)。

[本次实机证据与换电脑继续的说明](reports/2026-11-21-stage-test/README.md)。

[换电脑继续](CONTINUE.md)：接下来的主机工作暂时不需要设备。
