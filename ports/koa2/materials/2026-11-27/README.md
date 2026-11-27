# KOA2 下一次上机操作单（2026-11-27 套件）

这是操作单，在 Kindle 上按顺序做，不需要 WSL 或重新构建。English: [README.en.md](README.en.md)。

这次只有一项新测试：**执行模块空跑**。它第一次把跳转蹦床 `jump.S` 带上设备，但不跳转：
停 X 并卸载 EPDC、只读核验 USB 空闲、触摸休眠、CPU1 下线，然后加载 `koa2_handoff_exec.ko`，
让它重新核对静止状态和复制表、在自己的一页里装好蹦床并填好参数、屏蔽 IRQ/FIQ 读一次 CPU 寄存器，
最后停在关缓存之前，把一切按逆序放回。不写分区，不碰看门狗，`execute_available` 始终为 0。

| # | KUAL 菜单项 | 回答什么问题 | 会改动什么 |
|---|---|---|---|
| 1 | **Exec dry-run: quiesce, arm trampoline, stop before caches (no jump)** | 执行模块能否在原厂内核里加载，并在全部外设静止时装好蹦床、再完整恢复 | 显示、触摸、CPU1 约 20 秒不可用，脚本逆序恢复 |

每步动作前都先写并 `sync` 一行 `stage=`，卡死后报告末行的 `stage=` 就是没返回的那一步。只往 `koa2-exec-dryrun/` 写报告。

## 0. 先确认

Oasis 2、固件 5.16.2.1.1、已越狱并装好 KUAL、电量 50% 以上。不符合就停；脚本也会拒绝别的内核。

## 1. 把套件拷到 Kindle

只取这个文件夹：

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-27
git checkout
```

先删掉 Kindle 上旧的 `extensions/readom-koa2-stage-test`。把 `ports/koa2/materials/2026-11-27/readom-koa2-stage-test` 整个拷进 Kindle 的 `extensions/`，然后在 Kindle 盘上校验（盘符按实际改）：

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

16 行都要是 `OK`，第二条要输出 `{"items"`。看到 `%TSD-Head` 说明公司 DLP 加密了文件，换私人电脑拷。然后安全弹出 USB。

## 2. 执行模块空跑

**拔掉 USB 线**，关掉 Wi-Fi，停在原厂界面、屏幕亮着，**不要按电源键、不要锁屏**。KUAL → **Readom KOA2 Stage Test** → **Exec dry-run: quiesce, arm trampoline, stop before caches (no jump)**。
屏幕会黑十几秒，桌面随后回来。报告在 `koa2-exec-dryrun/<run>/report.txt`。

| result | 含义 | 下一步 |
|---|---|---|
| `exec_dryrun_restored` | **想要的结果**：蹦床装好又放回，静止判定通过；报告里 `exec_stopped_before_caches=1`、`exec_execute_available=0`、`exec_copylist=0`、`exec_params=0`、`exec_quiesce=0` | 点屏确认触摸，带回报告 |
| `exec_dryrun_restored_not_quiesced` | 蹦床装好又放回，但模块的静止判定拒绝了，多半因为 `/` 不是只读（`rootfs_readonly=0`） | 不算失败，带回报告 |
| `step_exec_insmod_refused` | 原厂内核拒绝加载执行模块；报告附 dmesg 里的版本行 | 带回报告，别的外设已恢复 |
| `step_exec_not_armed` | 模块加载了，但复制表重审或参数填写失败 | 带回报告 |
| `exec_restore_failed_restart_required` | 有一步没恢复，`restore_*=failed` 或 `fb_restored=0` 指出哪一步 | 正常重启，带回报告 |
| `step_*` | 某步没做成，前面的已恢复 | 带回报告 |
| `refused_*` | 前提不满足，什么都没改（插着 USB、开着 Wi-Fi、CPU1 不在线等） | 处理原因后重试 |

**如果卡死**（屏幕不动、触摸和按键都没反应）：等它自己重启，最多约 1 分钟；不重启就按住电源键约 40 秒。
回到原厂界面后**先**跑 KUAL → **Read previous boot recovery log**，再带回报告。不要重试。

通过后插回 USB，确认 Windows 能正常打开 Kindle 盘。

## 3. 带回报告并清理

```bash
cp -r /d/koa2-exec-dryrun ~/koa2-evidence/
```

在电脑上核对拷回的报告后，从 Kindle 删除 `extensions/readom-koa2-stage-test` 和这个报告目录。原始报告只留本地，不提交 Git。

## 4. 之后的实机门槛

这项通过后，才写真跳转：关缓存、进蹦床、启动 `headless-smoke` 内核，再靠看门狗回到原厂。动手前还要查清两件事：跳转前怎么喂看门狗，以及新内核入口 r1 的准确值。真跳转会另出套件，见 [recovery.md](../../recovery.md)。

## 5. 本套件的来源

`zImage` 和 `initramfs.cpio.gz` 与 2026-11-26 套件逐字节相同。新增 `koa2_handoff_exec.ko` 和 `exec-dryrun.sh`，菜单多了一项，`koa2-stage-client` 多了 `--exec-dryrun`。
`koa2_handoff_stage.ko` 相对 2026-11-26 套件有变：只有 `koa2_quiesce_verify` 一个函数不同，来自提交 813dc1d 给它加的 USB 空闲路径，导入表不变。
两个模块的原厂 CRC 核对都是 `passed: true`，见 [crcs.json](crcs.json)（暂存模块）和 [exec-crcs.json](exec-crcs.json)（执行模块）。
16 个文件的校验值在 `readom-koa2-stage-test/SHA256SUMS`，整个目录的在 [SHA256SUMS](SHA256SUMS)。主机测试 `koa2-exec`、`koa2-exec-wiring`、`koa2-exec-dryrun` 覆盖了空跑的每种拒绝、每一步失败和每一步恢复失败；`koa2-jump-baremetal` 在 QEMU 的 Cortex-A7 上跑了真的蹦床。
设计见[交接实现](../../../../boot/koa2/README.md)，完整流程见[实机操作手册](../../device-runbook.md)第 8d 节。
