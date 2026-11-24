# 回家要测的两件事（2026-11-24 套件）

这份文件就是操作单。拿着 Kindle 按顺序做，不需要开 WSL 或重新构建。English: [README.en.md](README.en.md)。

| # | 测试 | 回答什么问题 | 会动什么 |
|---|---|---|---|
| 1 | Inspect EPDC framebuffer users | EPDC 驱动的 3 个引用是谁占的 | 只读，什么都不停 |
| 2 | Touch only: sleep and wake controller | 原厂驱动让触摸休眠后，I²C 总线是否真的安静，唤醒后触摸是否恢复 | 触摸停约 12 秒，脚本自己唤醒 |

两项都不加载交接模块，不跳转，不写分区，不卸载驱动。写入只在 Kindle 的 `koa2-quiesce-dryrun/` 和 `koa2-touch-quiesce/` 两个目录。

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

11 行都是 `OK`，第二条打印 `{"items"` 才继续。打印 `%TSD-Head` 说明文件被公司 DLP 加密了，换家用电脑拷。安全弹出 USB。

## 2. EPDC 帧缓冲占用（先做，可以插着线）

KUAL → **Readom KOA2 Stage Test** → **Inspect EPDC framebuffer users**。几秒钟，屏幕不变。

报告在 `koa2-quiesce-dryrun/<运行编号>/report.txt`，最后一行是 `result=quiesce_blocked_pending_restore`（正常）。看 `[epdc_users]` 一节：

| `epdc_reference_accounting` | 含义 |
|---|---|
| `matched` | 列出的进程正好解释了全部引用。记下 `epdc_user_pid=… comm=…` 各行 |
| `shared_files_possible` | 进程数多于引用数，多半是 fork 后共享同一个打开的文件 |
| `more_references_than_openers` | 有引用不是用户空间打开的，源码没解释，报告原样带回 |
| `unavailable` | 没找到帧缓冲节点，报告带回 |

## 3. 触摸单项休眠与唤醒

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

## 4. 带回报告并清理

```bash
cp -r /d/koa2-quiesce-dryrun /d/koa2-touch-quiesce ~/koa2-evidence/
```

在电脑上确认副本能打开、内容完整后，从 Kindle 删掉 `extensions/readom-koa2-stage-test` 和这两个报告目录。报告只含内核身份、进程名、计数和状态，不含坐标、路径参数或屏幕内容；原始报告留本地，不进 Git。

## 5. 这份套件的来历

由分支提交 `8114e3e` 在 WSL 中构建（`ports/koa2/build-stage-test-kit.sh`），11 个文件的校验值在 `readom-koa2-stage-test/SHA256SUMS`，设备脚本运行前自己再核对一遍。模块导入的 35 个符号与原厂 CRC 全部一致（`crcs.json`）；这两项测试都不加载它。主机测试 22 项通过、0 项失败。设计和源码依据见[静止审计](../../handoff-quiesce.md)最后两节，完整流程见[实机操作手册](../../device-runbook.md)。
