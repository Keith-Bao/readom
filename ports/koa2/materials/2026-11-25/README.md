# KOA2 统一实机验收操作单（2026-11-25）

所有主机侧开发、编译与 32 项 KOA2 主机测试已全部完成。本操作单把剩余需要实机执行的测试收拢为一次连续批次。English: [README.en.md](README.en.md)。

## 0. 准备

- Oasis 2、固件 `5.16.2.1.1`、已越狱并安装 KUAL、电量 > 50%。
- 在 WSL（`Readom-KOA2`）中已构建好最新套件 `~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test`（13 个文件，`SHA256SUMS` 校验）。
- 复制到 Kindle 的 `extensions/readom-koa2-stage-test`，核对 13 项 SHA256 全 `OK` 后安全弹出并拔掉 USB。

## 1. 按顺序执行（均不写原厂分区，不跳转）

| # | KUAL 菜单项 | 验证目标 | 预期结果 |
|---|---|---|---|
| 1 | **Inspect EPDC framebuffer users** | 同时扫描 `/etc/upstart/` 与 `/etc/init/`，采集 `x.conf` 的 `start on`/`stop on` 依赖 | `epdc_reference_accounting=matched`、`epdc_upstart_*` |
| 2 | **Combined dry-run: USB + Touch + CPU1 (reversible, no jump)** | 联合验证 USB 卸载 + 触摸休眠 + CPU1 下线及逆序完整恢复（不碰 EPDC） | `result=full_quiesce_dryrun_restored`，重插线后 Windows 能正常读盘 |

## 2. 带回报告并清理

插回电脑后，把 `koa2-quiesce-dryrun/` 与 `koa2-full-quiesce-dryrun/` 拷回本地 `F:\Kindle\koa2-evidence\` 校验，再从 Kindle 删除 `extensions/readom-koa2-stage-test` 和这两个报告目录。原始报告留本地，不提交 Git。

## 3. 之后的实机推进门槛

1. 根据第 1 步采回的 `x.conf` 依赖完成 `epdc-quiesce.sh` 实机停用/重载验证。
2. 全部外设可逆静止通过后，再启用 `headless-smoke` 首次带看门狗自动回退的真机跳转（见 [recovery.md](../../recovery.md)）。
