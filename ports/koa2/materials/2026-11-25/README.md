# KOA2 下一次上机操作单（2026-11-25 套件）

这是操作单，在 Kindle 上按顺序做，不需要 WSL 或重新构建。English: [README.en.md](README.en.md)。

上一次（2026-11-25）联合空跑 v1 让 Kindle 卡死，看门狗把它重启回原厂系统，报告没有落盘，不知道停在哪一步。
本套件里的 v2 在每一步动作**之前**先写一行 `stage=` 并 `sync`，卡死后报告最后一行 `stage=` 就是没返回的那一步。
另外加了两个只跑两步的组合，用来缩小是哪两步一起出问题。

| # | KUAL 菜单项 | 回答什么问题 | 会改动什么 |
|---|---|---|---|
| 1 | Inspect EPDC framebuffer users | `x` 任务的 `start on`/`stop on` 依赖，决定之后怎样停 EPDC | 只读 |
| 2 | Combined pair: Touch + CPU1 only | 触摸休眠后再下线 CPU1 会不会卡死（v1 的头号嫌疑） | 触摸约 2 秒、CPU1 约 1 秒，脚本恢复 |
| 3 | Combined pair: USB + Touch only | 卸载 USB 存储后再让触摸休眠会不会卡死 | USB 和触摸约 2 秒，脚本恢复 |
| 4 | Combined dry-run: USB + Touch + CPU1 | 三步连在一起能否全部可逆恢复 | 三项约 3 秒，脚本逆序恢复 |

四项都不加载交接模块、不跳转、不写分区、不碰 EPDC。只往 `koa2-quiesce-dryrun/` 和 `koa2-full-quiesce-dryrun/` 写报告。

## 0. 先确认

Oasis 2、固件 5.16.2.1.1、已越狱并装好 KUAL、电量 50% 以上。不符合就停；脚本也会拒绝别的内核。

## 1. 把套件拷到 Kindle

只取这个文件夹：

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-25
git checkout
```

先删掉 Kindle 上旧的 `extensions/readom-koa2-stage-test`。把 `ports/koa2/materials/2026-11-25/readom-koa2-stage-test` 整个拷进 Kindle 的 `extensions/`，然后在 Kindle 盘上校验（盘符按实际改）：

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

13 行都要是 `OK`，第二条要输出 `{"items"`。看到 `%TSD-Head` 说明公司 DLP 加密了文件，换私人电脑拷。然后安全弹出 USB。

## 2. 只读采集（USB 可以插着）

KUAL → **Readom KOA2 Stage Test** → **Inspect EPDC framebuffer users**。几秒完成，屏幕不变。
报告在 `koa2-quiesce-dryrun/<run>/report.txt`，末行是 `result=quiesce_blocked_pending_restore`。
看 `[epdc_users]` 里 `epdc_reference_accounting=matched`，以及 `epdc_upstart_` 开头的几行。

## 3. 组合空跑，一项一项来

**拔掉 USB 线**，停在原厂界面、屏幕亮着，**测试期间不要按电源键、不要锁屏**。按 2 → 3 → 4 的顺序，每项做完看报告末行再决定下一项：

| result | 含义 | 下一步 |
|---|---|---|
| `full_quiesce_dryrun_restored` | **想要的结果**：各步都停下并恢复了 | 点一下屏幕确认触摸正常，做下一项 |
| `dryrun_restore_failed_restart_required` | 有一步没恢复，报告里 `restore_*=failed` 指出是哪一步 | 正常重启，带回报告，**后面的项不做** |
| `step_*` | 某一步没停下，已经恢复 | 带回报告，后面的项不做 |
| `refused_*` | 前提不满足，什么都没改（如 `refused_lun_not_proven_idle` 说明 USB 还插着） | 处理原因后可以再跑一次 |

**如果卡死**（屏幕不动、触摸和按键都没反应）：等它自己重启，最多约 1 分钟；不重启就按住电源键约 40 秒。
回到原厂界面后**先**跑 KUAL → **Read previous boot recovery log**，再停止本次所有测试，带回报告。不要重试同一项。

第 4 项通过后，插回 USB，确认 Windows 能正常打开 Kindle 盘。

## 4. 带回报告并清理

```bash
cp -r /d/koa2-quiesce-dryrun /d/koa2-full-quiesce-dryrun ~/koa2-evidence/
```

在电脑上核对拷回的报告后，从 Kindle 删除 `extensions/readom-koa2-stage-test` 和这两个报告目录。原始报告只留本地，不提交 Git。

## 5. 之后的实机门槛

1. 按第 2 步采回的 `x` 任务依赖，设计并单独验证 `epdc-quiesce.sh` 的停用与重载。
2. 外设全部可逆静止通过后，才做第一次带看门狗自动回退的 `headless-smoke` 真机跳转（见 [recovery.md](../../recovery.md)）。

## 6. 本套件的来源

在 WSL 中由 `ports/koa2/build-stage-test-kit.sh` 构建，并带上 `KOA2_STOCK_MODULES` 让构建先过原厂 CRC 检查：35 个导入全部一致，见 [crcs.json](crcs.json)。
13 个文件的校验值在 `readom-koa2-stage-test/SHA256SUMS`，整个目录的在 [SHA256SUMS](SHA256SUMS)。
`zImage` 与 2026-11-24 套件相同（`d2a93246…`，含 printk 缓存回写）。主机测试 `koa2-full-quiesce-dryrun` 覆盖了每一步失败、每一步恢复失败和每种拒绝。
设计和源码依据见[静止审计](../../handoff-quiesce.md)，完整流程见[实机操作手册](../../device-runbook.md)。
