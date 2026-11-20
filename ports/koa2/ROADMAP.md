# KOA2 自用适配执行路线

目标：Oasis 2 / 固件 5.16.2.1.1 保留原厂系统，运行 Android 8.1、微信读书，
支持触摸、实体翻页、旋转、前光、联网、USB、休眠和返回原厂。
English: [ROADMAP.en.md](ROADMAP.en.md)。当前进度见 [STATUS.md](STATUS.md)。

## 阶段与当前状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| A 开发记录 | 基线、命令、证据、下一步；每阶段独立提交 | 进行中 |
| B 环境 | WSL2 Ubuntu 22.04 `Readom-KOA2`，ext4 内构建 | 已完成，见 [build-environment.md](build-environment.md) |
| C 原厂内核 | zelda defconfig、zImage、DTB、modules、哈希；干净重建 | 已完成（headless-smoke），见 [headless-smoke.md](headless-smoke.md) |
| D Android 内核 | 逐项审计补丁，4.1 配置，Binder 多上下文优先 | 已审计未构建，见 [android41-audit.md](android41-audit.md) |
| E 交接 | 校验、暂存、复制表、跳转；SMP/DTB 后端 | 主机侧完成，设备侧未运行，见 [boot/koa2/README.md](../../boot/koa2/README.md) |
| F 最小 Linux | smoke initramfs，日志通路，三次往返 | initramfs 已就绪，未在设备上启动 |
| G 基础硬件 | EPDC v2、触摸、翻页、旋转、前光 | 未开始，缺 EPDC 源码，见 [epdc-source-gap.md](epdc-source-gap.md) |
| H AOSP | koa2-userdebug / Cortex-A7，独立输出 | 未开始 |
| I 日常功能 | Wi-Fi、ADB、MTP、微信读书、自动休眠 | 未开始 |
| J 安装回退 | KUAL 入口，机型校验，保留上一版 | 未开始 |

## E 阶段内部（当前重心）

| 件 | 状态 |
|---|---|
| DTB/内存契约校验 | 完成，测试 `koa2-contract` |
| 实测内存布局与排除表 | 完成，见 [boot-preflight-findings.md](boot-preflight-findings.md) |
| 内核占用推导（LZO + AUTO_ZRELADDR） | 完成，`ports/koa2/derive-kernel-footprint.py` |
| 布局校验 | 完成，测试 `koa2-plan` |
| 交接 FDT 改写 | 完成，测试 `koa2-fdt-rewrite` |
| 复制表与审计 | 完成，测试 `koa2-copylist` |
| 暂存模块（无跳转） | 完成，测试 `koa2-stage` |
| 原厂符号 CRC 核对 | 工具完成，缺原厂模块，测试 `koa2-module-crcs` |
| 跳转蹦床 | 完成并裸机验证，测试 `koa2-jump-baremetal`，未编进模块 |
| 静止顺序 | 只有可测的顺序，缺碰硬件的实现，测试 `koa2-quiesce` |
| 设备测试套件 | 完成，未运行，见 [device-runbook.md](device-runbook.md) |

## 阶段门槛

- E 推进到 F：设备上 `sealed_plan_valid`，且静止顺序的硬件实现齐备。
- F 推进到 G：用户手动启动，连续三次进入新内核并返回原厂。
- G 推进到 H：EPDC 源码缺口解决，刷新、触摸、翻页、旋转可用。
- H 推进到 I：桌面稳定 30 分钟并可返回原厂。

## 最终验收

- 10 次启动/返回、2 小时阅读、500 次实体翻页。
- 两个方向各旋转 20 次、20 次休眠唤醒、8 小时原厂/Android 待机对照。
- Wi-Fi 重连 10 次、USB 插拔切换 10 次。
- 正常升级、失败升级、系统回退各一次，原厂书库/KOReader/KUAL 正常。
- SELinux enforcing，ADB 需授权，无凭据泄露，所有构建输入有版本及哈希。
- 达标后才生成 KOA2 自用测试包；独立版本标识，本地保存签名密钥。

## 执行规则

每轮读取 [STATUS.md](STATUS.md)，核实 Git 和真实产物，完成当前未完成步骤并验证，
保存证据、提交推送、更新下一步。主机测试、模拟器和真机结果分开记录。
用户负责管理员操作/重启、Kindle 启动和恢复、应用登录；不购买云资源。
不删除保护检查、不强制加载不兼容模块、不写原始分区/IDME/密钥/校准。
无法证明交接可恢复时停止真机推进，交付源码和阻塞证据，不宣称完成。
