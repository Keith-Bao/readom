# 设备测试流程（Oasis 2）

> 2026-11-23 真机空跑后触摸未恢复，USB gadget 重载失败，EPDC 保持占用。禁用旧版空跑和真机跳转。新版 KUAL 入口 **Quiesce preflight (read-only)** 只采集驱动、输入设备、USB、根文件系统和 CPU 状态，正常报告以 `quiesce_blocked_pending_restore` 结尾。下文的停用步骤只用于解读旧版报告。

更新后的套件还提供 **Check live touch (tap screen)**。启动后 10 秒内轻点屏幕一次。探针以只读方式打开当前的 `cyttsp5_mt` 事件节点，报告 `touch_contact_observed` 表示收到了完整的接触事件帧。超时只表示该窗口未观察到触摸。探针不会保存坐标，预检报告仍以 `quiesce_blocked_pending_restore` 结尾。USB 部分记录原厂存储模块的 LUN 参数和控制器状态，电脑识别并读取 Kindle 盘需独立确认。

若空跑后已经重启，可用 **Read previous boot recovery log** 检查上一次启动的内核日志。报告只记录已知触摸和 USB 消息的类别及时间。先采集再考虑重启，因为缓冲只保留上一次启动。未发现错误行时，仍需检查日志是否读到，以及是否确实来自失败那次启动。

**USB 单项重载。** 安全弹出并拔掉 USB 线，然后在 KUAL 中运行 **USB only: reload storage (unplug USB first)**。仅当原厂内核、模块、空介质 LUN 和断开的 UDC 状态都吻合时，脚本才卸载 `g_mass_storage`，保留依赖模块并带着原参数重载。等待约 30 秒再插线。报告在 `koa2-usb-reload/<UTC时间>-<PID>/report.txt`。只有报告给出 `usb_idle_rebound_host_check_pending` 且电脑能重新读到 Kindle 盘，才算本次 USB 单项通过。若 30 秒后电脑仍识别不到 Kindle，重启以恢复原厂 USB，保留失败报告，不要重试。

这份文件是给**接手执行的人或 agent** 看的，不需要读过之前的对话。按顺序做，
每一步都写清楚了要什么、会得到什么、出问题怎么办。英文版见
[device-runbook.en.md](device-runbook.en.md)。

**这次要做的事**：把暂存模块装进原厂内核，让它在设备上检查交接的全部准备工作，
然后卸载。**不会跳转到新内核**，模块里没有任何代码能做到这件事。

**不会发生的事**：不写分区、不改引导、不改 IDME、不动校准数据、不联网，不主动请求重启；模块错误仍可能导致卡死或重启。
所有写入都在 Kindle 的 `koa2-stage-test/` 目录里。

**当前状态（2026-11-21）**：这套流程已在实机通过。换电脑继续不需要重跑，请先读[CONTINUE.md](CONTINUE.md)；只有暂存套件改动后需要重新验证时才再次使用本流程。

## 0. 先确认这些前提

| 前提 | 怎么确认 | 不满足怎么办 |
|---|---|---|
| Kindle 是 Oasis 2，固件 5.16.2.1.1 | `/etc/prettyversion.txt` | 停下。本套件只认这一个固件 |
| 内核是 `4.1.15-lab126` | `uname -a` | 停下。脚本自己也会拒绝 |
| Kindle 已越狱并装了 KUAL | 能看到 KUAL 菜单 | 停下，先处理越狱 |
| 电量 > 50% | 设备状态栏 | 先充电。电量不足先不要加载实验模块 |
| 构建主机是 WSL `Readom-KOA2` | `wsl.exe -l -v` | 见 [build-environment.md](build-environment.md) |

## 1. 在主机上造套件

在 WSL 里（不要在 Windows 侧操作仓库文件，公司 DLP 会加密 `.py`/`.json`）：

前提：已经按[构建环境](build-environment.md)准备 headless-smoke 源码、构建输出和 ARM 工具链。仅克隆仓库不能直接生成套件。

```bash
set -e
cd ~/readom-work/readom
bash ports/koa2/build-smoke-initramfs.sh
bash ports/koa2/build-stage-test-kit.sh
```

产物在 `~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/`，共 8 个文件：
`stage-test.sh`、`koa2_handoff_stage.ko`、`koa2-stage-client`、`zImage`、
`initramfs.cpio.gz`、`SHA256SUMS`、`menu.json`、`config.xml`。

脚本在导出前自己会拒绝这些情况：模块需要 GOT、导入了构建树没导出的符号、
写协处理器寄存器、导入 `cpu_down`/重启类符号、zImage 与模块钉的哈希不一致。
**它拒绝就不要往设备上拷**，先解决。

## 2. 拷到 Kindle

USB 连上，在 WSL 里以 root 挂载并复制：

示例使用本次实测的 D:，盘符并不固定，须先在 Windows 确认。在单独 shell 中执行：命令遇错停止，已有同名扩展目录时也会停止；需要替换时先保留旧套件。

```bash
set -e
# Confirm the Kindle drive in Windows first. D: was used on 2026-11-21.
sudo mkdir -p /mnt/kindle
sudo mount -t drvfs D: /mnt/kindle
test -d /mnt/kindle/documents && test -d /mnt/kindle/extensions
test ! -e /mnt/kindle/extensions/readom-koa2-stage-test
cp -r ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test /mnt/kindle/extensions/
```

复制完必须验这一句，确认 DLP 没有加密 JSON：

```bash
cd /mnt/kindle/extensions/readom-koa2-stage-test
sha256sum -c SHA256SUMS
cmp ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/menu.json menu.json
cmp ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/config.xml config.xml
head -c 8 menu.json
cd ~
sync
sudo umount /mnt/kindle
```

必须输出 `{"items"`。如果是 `%TSD-Head`，说明文件被加密了，KUAL 读不出菜单，
换一种复制方式（别经 Windows 资源管理器）。`.ko`、zImage 和脚本被改动时设备上的
哈希检查会拒绝运行，但 `menu.json` 和 `config.xml` 不在哈希表里，应按上面的 cmp 命令与原件逐字节核对。

然后在 Windows 里安全弹出 USB，再在 Kindle 上操作。

## 3. 在 Kindle 上运行

KUAL → **Readom KOA2 Stage Test** → **Stage and check handoff (no jump)**。

约几十秒。期间屏幕不会变化，这是正常的：测试不碰显示。

脚本依次做：核对内核身份和套件哈希 → 只读复制原厂 `.ko` → 记录
`panic_on_oops`/内存/dmesg → `insmod` → 暂存 zImage、运行时 FDT、initramfs →
封存 → `rmmod` → 再记录一次。

## 4. 读结果

报告在 Kindle 的 `koa2-stage-test/<UTC时间>-<PID>/`，`LAST_RUN.txt` 指向最新一次。
看 `report.txt` 末尾的 `result=`：

| result | 含义 | 下一步 |
|---|---|---|
| `insmod_refused` | 加载失败；具体可能为 CRC、身份或其他原因，须看 dmesg。本次已记录套件实机加载成功 | 走第 6 节 |
| `sealed_plan_valid` | 暂存封存检查通过；还需确认 rmmod_exit=0 | 走第 7 节 |
| `seal_failed` | 模块装上了但封存被拒 | 看 `seal_stage`，对照第 5 节 |
| `refused_unknown_kernel` | 内核不是 `4.1.15-lab126` | 确认设备和固件，什么都没加载 |
| `refused_kit_damaged` | 套件哈希不符 | 重新执行第 1、2 节 |
| `refused_module_already_loaded` | 模块已经在内核里 | 先 `rmmod koa2_handoff_stage` |

`client_exit=0` 之前的 STATUS 行给出数据，含义见下：

| 字段 | 应该是 |
|---|---|
| `state` | 2（已封存） |
| `plan_valid` | 1 |
| `execute_available` | **恒为 0**。这个阶段没有跳转，是 1 就说明装错了东西 |
| `fdt_bytes` | 约 47900（原厂 FDT 改写后，比原件小） |
| `payload_pages` | 本次 1706；取决于三个载荷的实际大小 |
| `collision_pages` | 被避开的页数，0 到 8192。超过 8192 会以 `-ENOSPC` 失败 |
| `list_pages`/`list_entries` | 复制表的规模，`list_entries` 约等于 `payload_pages + 3 + list_pages` |

## 5. 封存失败时 seal_stage 的含义

| seal_stage | 卡在哪 |
|---|---|
| 1 | 载荷没写全 |
| 2 | 某个载荷的哈希与声明不符（`seal_result` 是槽号：0 zImage、1 FDT、2 initramfs） |
| 3 | zImage 不是构建时钉住的那一个 |
| 4 | 原厂运行时 FDT 结构不合法或不是 Zelda |
| 5 | FDT 里有 memreserve 不在实测排除表内（**说明这台设备的保留区与记录不同，重要**） |
| 6 | 改写 FDT 失败 |
| 7 | `/memory` 不是 0x80000000 + 0x20000000 |
| 8 | 布局校验失败 |
| 9 | 有暂存页落在目的地址上 |
| 10 | 复制表审计失败 |

5、7、9、10 任何一个出现都要先停下分析，不要重试——它们说明这台设备的内存布局
与 [boot-preflight-findings.md](boot-preflight-findings.md) 记录的不一样。

## 6. 如果是 insmod_refused

把报告拷回主机并核对 CRC：

```bash
set -e
# Use the actual drive letter; mount only if not already mounted.
sudo mkdir -p /mnt/kindle
sudo mount -t drvfs D: /mnt/kindle
mkdir -p ~/koa2-evidence/stage-test
cp -a /mnt/kindle/koa2-stage-test/. ~/koa2-evidence/stage-test/
sudo umount /mnt/kindle
```

```bash
cd ~/readom-work/readom && R=~/koa2-evidence/stage-test/$(cat ~/koa2-evidence/stage-test/LAST_RUN.txt) && python3 ports/koa2/module-crcs.py check --stock $R/stock-modules --symvers ~/readom-work/out-headless-smoke/Module.symvers ~/artifacts/koa2-handoff-module/koa2_handoff_stage.ko
```

输出里 `differ` 是 CRC 不一致的符号，`unknown` 是原厂模块里查不到的符号。
两者都为空而 `passed` 为 true 时，模块本该能装上（那就去看 dmesg 里 `insmod` 的真正原因）。

有不一致时，按 [boot/koa2/README.md](../../boot/koa2/README.md)「原厂内核的版本检查」处理：
换掉相应的导入，或用原厂配置重编那些符号涉及的类型。改完重新执行第 1 节，
并带上原厂模块让构建脚本自己先验一遍：

```bash
KOA2_STOCK_MODULES=$R/stock-modules bash ports/koa2/build-stage-test-kit.sh
```

然后回到第 2 节。

## 7. 如果是 sealed_plan_valid

本套件的暂存检查通过，不代表全部交接工作已经验证。确认 client_exit=0、rmmod_exit=0 且 execute_available=0。把报告拷回主机（命令同第 6 节），
更新 [STATUS.md](STATUS.md)，然后**停在这里**。

下一个阶段是真正的交接执行，它还缺两样东西，都不在这份流程里：
静止顺序里真正碰硬件的 run/undo（[quiesce.c](../../boot/koa2/quiesce.c) 目前只有可测的顺序），
以及在交接当次读出 EPDC 帧缓冲地址加入排除区。
跳转蹦床（[jump.S](../../boot/koa2/jump.S)）已写好并在 QEMU 的 Cortex-A7 上裸机跑过，
但没有编进任何模块，也从未在设备上执行。

## 8. 出意外时怎么办

| 现象 | 原因 | 怎么办 |
|---|---|---|
| 设备在 `insmod` 之后卡住或重启 | 模块在原厂内核里出错（oops） | 等。`panic_on_oops` 为 1 时原厂内核约 5 秒后自己重启回原厂；为 0 时可能仅终止进程，也可能留下无法继续使用的内核状态 |
| 屏幕没反应但设备还活着 | 测试不碰显示，正常 | 等脚本结束，看报告 |
| 完全不响应 | — | 长按电源键约 40 秒强制重启。不能依赖 60 秒自动恢复：内核仍运行时可能持续喂狗 |
| 重启后担心数据 | 没有写过任何分区 | 预期从原厂路径启动；恢复后检查书库和 KUAL |

重启走原厂路径时，原厂内核会照常把 printk 写到 eMMC 扇区 0x9a400。
这是它对任何崩溃都会做的事，与交接无关，见
[boot-preflight-findings.md](boot-preflight-findings.md) 末节。

## 8b. 静止空跑（下一次要在设备上做的事）

暂存测试已经通过，所以设备上的下一步是这个：**停掉能停的设备，看一眼，再全部放回**。
KUAL → Readom KOA2 Stage Test → **Quiesce dry run (stops devices, restores them)**。

它不加载交接模块、不复制载荷、不跳转。做完之后原厂系统照常运行。

依次停：EPDC（屏幕会停在最后一幅画面，这是正常的）、Wi-Fi、USB gadget、触摸和传感器；
然后 sync 并把根文件系统只读重挂；然后把次核下线。检查完就按相反顺序全部放回。

报告在 `koa2-quiesce-dryrun/<UTC时间>-<PID>/report.txt`，看最后的 `result=`：

| result | 含义 | 下一步 |
|---|---|---|
| `quiesced_and_restored` | 这条路走得通：四个主设备都停下了，根文件系统只读，次核下线，然后全部放回 | 把报告拷回主机，看各步耗时，再谈交接执行 |
| `incomplete_see_above` | 有东西没停下来。报告里 `IN_USE=`、`REMOVE_FAILED=`、`ROOTFS_REMOUNT_FAILED` 指出是哪个 | 停下分析。**不要重试**：某个驱动停不掉，交接就不能做 |
| `refused_unknown_kernel` | 内核不是 `4.1.15-lab126` | 什么都没动 |

无论成功还是失败，脚本都会把停掉的东西放回去（报告末尾的 `[restore]` 段），
放回顺序是：先把根文件系统恢复可写，再重新加载驱动，最后让次核上线。
万一有 `RELOAD_FAILED=` 或 `CPU1_STILL_DOWN`，重启一次设备就全好了——没有写过任何分区。

Wi-Fi 和触摸会停几秒，这是预期的。屏幕在 EPDC 停掉之后不会再刷新，直到它被放回。

## 8c. 标记测试（放一个标记，重启，确认它还在）

这件事要分两次，因为要测的正是"标记能不能活过复位"。同一次启动里读不到自己写的标记：
`/proc/printkbuf` 发布的是**上一次**启动的缓冲。

它不跳转、不复制载荷、不写分区、不下核。代价是这一次启动的内核日志最后 256 字节。
为什么需要它，见 [recovery.md](recovery.md) 第四节。

**第一步。** KUAL → Readom KOA2 Stage Test → **Marker test: place one (no jump)**。
报告在 `koa2-marker-test/<UTC时间>-<PID>/report.txt`：

| result | 含义 | 下一步 |
|---|---|---|
| `marker_placed` | 标记写进了 printk 缓冲并刷出了缓存 | 重启设备，做第二步 |
| `mark_failed` | 模块拒绝写（`client_exit` 非 0） | 停下看报告，不要重试 |
| `insmod_refused` | 模块没加载，和标记无关 | 按第 6 节处理 |
| `refused_unknown_kernel` | 内核不是 `4.1.15-lab126` | 什么都没动 |

**第二步：重启 Kindle**，然后 KUAL → **Marker test: check after restart**。
正常重启就行，不必拔电或等看门狗。

| result | 含义 | 说明了什么 |
|---|---|---|
| `marker_survived` | 上一次启动的缓冲里有 `KOA2_HANDOFF` 那一行 | 标记这条路通了，而且 [recovery.md](recovery.md) 第二节（复位保留 DRAM）得到了实机确认 |
| `marker_absent` | 没找到 | **停下**。要么标记没真正落到 DRAM，要么复位并不保留 DRAM——后者意味着第二节的推断是错的，比标记本身要紧得多，不要在这上面继续往跳转推进 |
| `refused_no_printkbuf` | 没有 `/proc/printkbuf` | 内核没带 `CONFIG_LAB126_PRINTK_BUFFER`，整条日志路线不成立 |

第二步只把含 `KOA2_HANDOFF` 的行留在报告里。上一次启动的完整日志是设备自己的，
读完即删，不进 Git。

## 9. 还需要设备、但不在这次流程里的事

| 事 | 为什么必须上机 |
|---|---|
| 标准 sdhci 驱动在这块 eMMC 上的速度模式与 tuning | 只能上机测 |
| EPDC 帧缓冲每次启动的实际地址 | 动态分配，要当次读 |
| 静止顺序每一步对硬件的作用 | 现在只验了顺序和回滚 |
| printk 缓冲能否活过 WDOG_B 复位 | 只能上机测 |
| 交接执行（复制并跳转） | 以上都确认之后才谈 |
