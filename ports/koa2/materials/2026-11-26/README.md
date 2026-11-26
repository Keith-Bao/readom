# KOA2 下一次上机操作单（2026-11-26 套件）

这是操作单，在 Kindle 上按顺序做，不需要 WSL 或重新构建。English: [README.en.md](README.en.md)。

2026-11-25 实机确认了什么、本套件改了什么：
- **EPDC 停用/重载可行。** `stop x` 释放了全部 3 个 `/dev/fb0` 引用（`epdc_users_after_stop=0`），模块卸载（0.07 秒）、按原厂波形重载（0.21 秒）都成功。唯一卡住的是恢复：`initctl start x` 不返回。本套件的 `epdc-quiesce.sh` 是 **v3**，改用 `--no-wait` 启动 x（原厂 `/etc/upstart/display` 的做法），调用立刻返回，成败由已有的 30 秒 fb0 重开轮询判定。
- **联合卡死的根因是 `rmmod g_mass_storage`。** USB+Touch 组合精确卡在 `stage=usb_unload`，还没到触摸步骤；Touch+CPU1 则干净通过。本套件的 `full-quiesce-dryrun.sh` 是 **v3**，不再卸载 gadget 模块：往 UDC `soft_connect` 节点写 `disconnect`，恢复时写 `connect`，控制器同步停止再启动，没有 `rmmod` 的拆卸等待。

| # | KUAL 菜单项 | 回答什么问题 | 会改动什么 |
|---|---|---|---|
| 1 | **EPDC only: stop X, unload and reload (reversible)** | `start x --no-wait` 能不能把显示拉回来？（2026-11-25 唯一卡住的那步） | X/框架约 15 秒不可用，脚本恢复 |
| 2 | Combined pair: USB + Touch only | `soft_connect` 断连（不 `rmmod`）能不能避开 `rmmod g_mass_storage` 造成的卡死？ | USB 和触摸约 2 秒，脚本恢复 |
| 3 | Combined dry-run: USB + Touch + CPU1 | 三步连在一起、用新 USB 路径能否全部可逆恢复 | 三项约 3 秒，脚本逆序恢复 |

第 1 项会重载 EPDC 模块，但不跳转、不写分区、不加载交接模块；第 2、3 项不碰 EPDC。每步动作前都先写并 `sync` 一行 `stage=`，卡死后报告末行的 `stage=` 就是没返回的那一步。只往 `koa2-epdc-quiesce/` 和 `koa2-full-quiesce-dryrun/` 写报告。

## 0. 先确认

Oasis 2、固件 5.16.2.1.1、已越狱并装好 KUAL、电量 50% 以上。不符合就停；脚本也会拒绝别的内核。

## 1. 把套件拷到 Kindle

只取这个文件夹：

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2-quiesce-preflight https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-26
git checkout
```

先删掉 Kindle 上旧的 `extensions/readom-koa2-stage-test`。把 `ports/koa2/materials/2026-11-26/readom-koa2-stage-test` 整个拷进 Kindle 的 `extensions/`，然后在 Kindle 盘上校验（盘符按实际改）：

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
head -c 8 menu.json
```

14 行都要是 `OK`，第二条要输出 `{"items"`。看到 `%TSD-Head` 说明公司 DLP 加密了文件，换私人电脑拷。然后安全弹出 USB。

## 2. 第 1 项 —— EPDC 停用与重载（v3 恢复修复）

**拔掉 USB 线**，停在原厂界面、屏幕亮着。KUAL → **Readom KOA2 Stage Test** → **EPDC only: stop X, unload and reload (reversible)**。
停 X、重载模块、重启 X 期间屏幕会黑最多约 15 秒。报告在 `koa2-epdc-quiesce/<run>/report.txt`。

| result | 含义 | 下一步 |
|---|---|---|
| `epdc_quiesce_restored` | **想要的结果**：模块已重载、X 回来了；`x_started=1`、`fb_restored=1`、`x_status_after=x start/running` | 点屏确认触摸，做第 3 步 |
| `epdc_gui_restore_timeout` | 模块重载了，但 Xorg 30 秒内没重开 fb0；`x_status_after`/`lab126_gui_status_after` 指出停在哪 | 带回报告；正常重启可恢复桌面 |
| `epdc_x_start_failed_restart_required` | `start x --no-wait` 本身失败 | 带回报告，重启 |
| `epdc_reload_failed_restart_required` | 模块没能重载 | 带回报告，重启 |
| `refused_*` | 前提不满足，什么都没改 | 处理原因后重试 |

显示若没自动回来，正常重启总能恢复原厂桌面（模块和波形都是原厂的）。

## 3. 用新 USB 路径跑组合空跑

仍拔着线、停在原厂界面，**不要按电源键、不要锁屏**。先跑 **USB + Touch only**，再跑 **USB + Touch + CPU1**，每项先看报告末行：

| result | 含义 | 下一步 |
|---|---|---|
| `full_quiesce_dryrun_restored` | **想要的结果**：各步都停下并恢复；USB 显示 `step_usb=disconnected` 再 `restore_usb=ok` | 点屏确认触摸，做下一项 |
| `step_usb_not_stopped` | UDC 没离开 configured 状态 | 带回报告，后面不做 |
| `dryrun_restore_failed_restart_required` | 有一步没恢复，`restore_*=failed` 指出哪一步 | 正常重启，带回报告，后面不做 |
| `step_*` | 某步没停下，前面的已恢复 | 带回报告，后面不做 |
| `refused_usb_host_connected` | USB 还插在主机上 | 拔线重试 |
| `refused_*` | 其它前提不满足，什么都没改 | 处理原因后重试 |

**如果卡死**（屏幕不动、触摸和按键都没反应）：等它自己重启，最多约 1 分钟；不重启就按住电源键约 40 秒。
回到原厂界面后**先**跑 KUAL → **Read previous boot recovery log**，再停止本次测试，带回报告。不要重试同一项。

USB+Touch+CPU1 那项通过后，插回 USB，确认 Windows 能正常打开 Kindle 盘。

## 4. 带回报告并清理

```bash
cp -r /d/koa2-epdc-quiesce /d/koa2-full-quiesce-dryrun ~/koa2-evidence/
```

在电脑上核对拷回的报告后，从 Kindle 删除 `extensions/readom-koa2-stage-test` 和这两个报告目录。原始报告只留本地，不提交 Git。

## 5. 之后的实机门槛

1. EPDC 可逆停用/重载与 `soft_connect` USB 路径都确认后，所有外设都有了经证实的可逆静止。
2. 到那时才做第一次带看门狗自动回退的 `headless-smoke` 真机跳转（见 [recovery.md](../../recovery.md)）。

## 6. 本套件的来源

二进制（`zImage`、`koa2_handoff_stage.ko`、`initramfs.cpio.gz`、`koa2-stage-client`、`touch-events`）与 2026-11-24/25 经实机验证的构建逐字节相同：35 个原厂 CRC 导入全部一致，见 [crcs.json](crcs.json)。相对 2026-11-25 套件只改了 `epdc-quiesce.sh`（v3）和 `full-quiesce-dryrun.sh`（v3）。
14 个文件的校验值在 `readom-koa2-stage-test/SHA256SUMS`，整个目录的在 [SHA256SUMS](SHA256SUMS)。主机测试 `koa2-epdc-quiesce` 与 `koa2-full-quiesce-dryrun` 覆盖了每一步失败、每一步恢复失败和每种拒绝。
设计和源码依据见[静止审计](../../handoff-quiesce.md)，完整流程见[实机操作手册](../../device-runbook.md)。
