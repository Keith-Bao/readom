# 回家要测的五件事（2026-11-24 套件）

这份文件就是操作单。拿着 Kindle 按顺序做，不需要开 WSL 或重新构建。English: [README.en.md](README.en.md)。

| # | 测试 | 回答什么问题 | 会动什么 |
|---|---|---|---|
| 1 | Inspect EPDC framebuffer users | EPDC 驱动的 3 个引用是谁占的 | 只读，什么都不停 |
| 2 | Inspect Wi-Fi (bcmdhd) users & params | `bcmdhd` 模块引用、安全参数和关联进程是谁 | 只读，什么都不停 |
| 3 | Backup stock runtime DTB (read-only) | 补齐本地缺失的原厂运行时 `/sys/firmware/fdt` 备份 | 只读，仅复制到报告目录 |
| 4 | CPU1 only: offline and online | 原厂 GPCv2 次核下线后是否安静，并能正常重新上线 | CPU1 下线约 2 秒，脚本自己拉起 |
| 5 | Touch only: sleep and wake controller | 原厂驱动让触摸休眠后，I²C 总线是否真的安静，唤醒后触摸是否恢复 | 触摸停约 12 秒，脚本自己唤醒 |

五项都不加载交接模块，不跳转，不写分区，不卸载驱动。写入只在 Kindle 的 `koa2-quiesce-dryrun/`、`koa2-cpu1-quiesce/` 和 `koa2-touch-quiesce/` 三个目录。

## 0. 先确认

Oasis 2、固件 5.16.2.1.1、已越狱并装了 KUAL、电量高于 50%。不满足就停下；脚本也会拒绝其他内核。

## 1. 把套件拷到 Kindle

只下载这个文件夹：

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-24
git checkout
```

`extensions/` 里如果还有旧的 `readom-koa2-stage-test`，先移出去。把 `ports/koa2/materials/2026-11-24/readom-koa2-stage-test` 整个目录拷到 Kindle 的 `extensions/`，然后在 Kindle 盘上核对（盘符按实际改）：

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

12 行都是 `OK`，第二条打印 `{"items"` 才继续。打印 `%TSD-Head` 说明文件被公司 DLP 加密了，换家用电脑拷。安全弹出 USB。

## 2. 三项只读采集（可以插着线，依次点完）

在 KUAL → **Readom KOA2 Stage Test** 里依次运行以下三项（每项几秒钟，屏幕不变，报告都在 `koa2-quiesce-dryrun/<运行编号>/report.txt`，最后一行都是 `result=quiesce_blocked_pending_restore`）：

1. **Inspect EPDC framebuffer users**：看 `[epdc_users]` 的 `epdc_reference_accounting`（`matched` / `shared_files_possible` / `more_references_than_openers` / `unavailable`）与进程列表。
2. **Inspect Wi-Fi (bcmdhd) users & params**：看 `[wifi_users]` 的模块引用、接口状态、SDIO 绑定与关联进程名（不含 MAC/SSID/密码）。
3. **Backup stock runtime DTB (read-only)**：看 `[runtime_fdt]` 是否打印 `fdt_backup=ok`、`fdt_magic=d00dfeed` 及 SHA256；同目录会生成 `stock-runtime.dtb`（只留本地，**不要提交 Git**）。

## 3. CPU1 单项下线与恢复

KUAL → **CPU1 only: offline and online**。几秒钟完成，报告在 `koa2-cpu1-quiesce/<运行编号>/report.txt`：

| result | 含义 | 接着做什么 |
|---|---|---|
| `cpu1_quiesce_restored` | **想要的结果**：次核成功下线、离线期间无中断并重新上线 | 继续下一步 |
| `cpu1_restore_failed_restart_required` | 次核没能重新上线 | 正常重启一次，带回报告，不要重试 |
| 其他 `cpu1_*` / `refused_*` | 报告里写明失败原因 | 带回报告，不要重试 |

## 4. 触摸单项休眠与唤醒

拔掉 USB，保持原厂系统，屏幕亮着，**测试期间不要按电源键、不要锁屏**。

KUAL → **Touch only: sleep and wake controller (tap after)**。前约 12 秒触摸不响应，这是预期的；之后十秒内轻点屏幕一次。

报告在 `koa2-touch-quiesce/<运行编号>/report.txt`：

| result | 含义 | 接着做什么 |
|---|---|---|
| `touch_quiesce_restored_touch_observed` | **想要的结果**：休眠期间总线和触摸中断为 0，电源已断，醒后收到触摸 | 带回报告 |
| `touch_quiesce_restored_touch_unconfirmed` | 静止成立，醒后十秒内没收到触摸 | 点一下原厂界面确认触摸正常，带回报告 |
| `touch_wake_failed_restart_required` | 唤醒没成功 | 正常重启一次，触摸就会恢复。不要重试 |
| 其他 `touch_*` | 报告里写明哪一条不满足（总线不安静、电源没断、期间锁屏等），已尝试唤醒 | 确认触摸可用，带回报告，不要重试 |
| `refused_*` | 前提不满足，什么都没写 | 带回报告 |

触摸若测完仍不响应：正常重启。完全不响应时长按电源键约 40 秒。

## 5. 带回报告并清理

```bash
cp -r /d/koa2-quiesce-dryrun /d/koa2-cpu1-quiesce /d/koa2-touch-quiesce ~/koa2-evidence/
```

在电脑上确认副本和 `stock-runtime.dtb` 都能打开、内容完整后，从 Kindle 删掉 `extensions/readom-koa2-stage-test` 和这三个报告目录。原始报告和 `stock-runtime.dtb` 留本地，不进 Git。

## 6. 这份套件的来历

在 WSL 中由 `ports/koa2/build-stage-test-kit.sh` 构建（含最新已补齐 printk 缓存回写的 `zImage` 与对应暂存模块），12 个文件的校验值在 `readom-koa2-stage-test/SHA256SUMS`。以上五项测试均不加载交接模块。设计和源码依据见[静止审计](../../handoff-quiesce.md)，完整流程见[实机操作手册](../../device-runbook.md)。
