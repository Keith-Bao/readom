# 回家要测的两件事（2026-11-23 套件）

这份文件就是今晚的操作单。拿着 Kindle 按顺序做，不需要读别的文档，也不需要开 WSL
或重新构建。English: [README.en.md](README.en.md)。

**要测什么**：两件事，顺序固定（先标记测试，后静止空跑），加起来约 15 分钟。

| # | 测试 | 它回答什么问题 | 风险 |
|---|---|---|---|
| 1 | 标记测试（放一个、重启、再确认） | 复位之后 DRAM 还在不在。这是[恢复路径](../../recovery.md)第二节唯一还只靠源码推断的结论 | 代价是那一次启动的内核日志最后 256 字节 |
| 2 | 静止空跑 | 能不能把会碰内存的设备都停掉、再全部放回 | 屏幕、Wi-Fi、触摸会停十几秒，脚本自己放回 |

**都不会跳转**。不写分区、不改引导、不动 IDME、不碰校准数据、不联网。
所有写入都在 Kindle 的 `koa2-marker-test/` 和 `koa2-quiesce-dryrun/` 两个目录里。
跳转蹦床（`boot/koa2/jump.S`）没有编进这个套件里的任何东西。

## 0. 先确认

| 前提 | 怎么确认 | 不满足怎么办 |
|---|---|---|
| 机器是 Oasis 2，固件 5.16.2.1.1 | 设置里看，或 `/etc/prettyversion.txt` | 停下。套件只认这一个固件，脚本自己也会拒绝 |
| 已越狱并装了 KUAL | 能看到 KUAL 菜单 | 停下 |
| 电量 > 50% | 状态栏 | 先充电 |

## 1. 把套件拷到 Kindle

套件在这个文件夹的 `readom-koa2-stage-test/`，10 个文件，不用构建，直接拷。

**在家用电脑上**（没有公司 DLP，可以直接用资源管理器）：下载这个文件夹，
整个 `readom-koa2-stage-test` 目录放进 Kindle 的 `extensions/` 里。

只下载这一个文件夹，不必克隆整个分支（分支约 200 MB）：

```bash
git clone --filter=blob:none --no-checkout --depth 1 -b port/koa2 https://github.com/Keith-Bao/readom.git
cd readom
git sparse-checkout set ports/koa2/materials/2026-11-23
git checkout
```

Kindle 插 USB 之后，目标位置是 `<Kindle盘>/extensions/readom-koa2-stage-test/`。
**上一次的套件（11-21 那份）如果还在，先把它从 `extensions/` 里移出来**——同名、
菜单也同名，留着会分不清今晚跑的是哪一份。移到 Kindle 根目录随便一个别的名字就行。

拷完**必须**在 Kindle 盘上核对一次，确认文件没在复制途中被改动：

```bash
cd /d/extensions/readom-koa2-stage-test && sha256sum -c SHA256SUMS
```

七行全是 `OK` 才继续。然后确认菜单文件是明文（`menu.json` 和 `config.xml` 不在哈希表里）：

```bash
head -c 8 /d/extensions/readom-koa2-stage-test/menu.json
```

必须打印 `{"items"`。如果打印的是 `%TSD-Head`，文件被公司 DLP 加密了，KUAL 读不出菜单——
这只会发生在公司电脑上，换家用电脑拷，或按[设备手册](../../device-runbook.md)第 2 节从 WSL 里拷。

安全弹出 USB，再在 Kindle 上操作。

## 2. 标记测试（先做这个，要重启一次）

**为什么要重启**：要测的正是"标记能不能活过复位"。同一次启动里读不到自己写的标记，
`/proc/printkbuf` 发布的是**上一次**启动的缓冲。所以这件事必须分两步。

**第一步**：KUAL → **Readom KOA2 Stage Test** → **Marker test: place one (no jump)**。
几秒钟。屏幕不会变化，这是正常的，测试不碰显示。

报告在 Kindle 的 `koa2-marker-test/<UTC时间>-<PID>/report.txt`：

| result | 含义 | 接着做什么 |
|---|---|---|
| `marker_placed` | 标记写进了 printk 缓冲并刷出了缓存 | 重启设备，做第二步 |
| `mark_failed` | 模块拒绝写 | 停下，把报告留着，不要重试 |
| `insmod_refused` | 模块没加载，和标记无关 | 停下，报告里的 dmesg 几行是关键 |
| `refused_unknown_kernel` | 内核不是 `4.1.15-lab126` | 什么都没动，确认机型和固件 |

**第二步：重启 Kindle**（正常重启就行，不用拔电，也不必等看门狗），
然后 KUAL → **Marker test: check after restart**。

| result | 含义 | 说明了什么 |
|---|---|---|
| `marker_survived` | 上一次启动的缓冲里有 `KOA2_HANDOFF` 那一行 | **这是想要的结果**：标记这条路通了，而且复位保留 DRAM 得到实机确认 |
| `marker_absent` | 缓冲读出来了（报告里 `previous_boot_log_lines` 是几百行），但里面没有标记 | **停下**。要么标记没真正落到 DRAM，要么复位不保留 DRAM；后者意味着[恢复路径](../../recovery.md)第二节的推断是错的，比标记本身要紧得多 |
| `refused_buffer_unreadable` | 缓冲没读出来（`previous_boot_log_lines=0`） | 不是标记的问题，是读取本身没成功。**不要**当成 `marker_absent` 来理解 |
| `refused_no_printkbuf` | 没有 `/proc/printkbuf` | 内核没带 `CONFIG_LAB126_PRINTK_BUFFER`，整条日志路线不成立 |

`refused_buffer_unreadable` 这个结果是 2026-11-23 加的。原来的脚本用 `strings` 取文本，
而这台 Kindle 的 busybox 有没有这个 applet 没有任何证据（只证实过 `tr`）；
applet 缺失时管道什么都不输出，脚本会把它当成 `marker_absent` 报出来——
正好是那个最吓人的结论，而工具其实根本没运行。现在读取必须先自证成功
（每次启动都有几百行带时间戳的内核日志），读不到就单独报这一个结果。

第二步只把含 `KOA2_HANDOFF` 的行留在报告里。上一次启动的完整日志是设备自己的，
读完就删，不进 Git。

## 3. 静止空跑（标记测试做完再做）

KUAL → **Readom KOA2 Stage Test** → **Quiesce dry run (stops devices, restores them)**。

它不加载交接模块、不复制载荷、不跳转。做完之后原厂系统照常运行。

依次停：EPDC（**屏幕会停在最后一幅画面，这是正常的**）、Wi-Fi、USB gadget、触摸和传感器；
然后 sync 并把根文件系统只读重挂；然后把次核下线。检查完按相反顺序全部放回。
Wi-Fi 和触摸会停十几秒，这是预期的。屏幕在 EPDC 放回之后才会再刷新。

报告在 `koa2-quiesce-dryrun/<UTC时间>-<PID>/report.txt`：

| result | 含义 | 接着做什么 |
|---|---|---|
| `quiesced_and_restored` | **这是想要的结果**：四个主设备都停下了，根文件系统只读，次核下线，然后全部放回 | 看报告里各步耗时 |
| `incomplete_see_above` | 有东西没停下来，报告里 `IN_USE=`、`REMOVE_FAILED=`、`ROOTFS_REMOUNT_FAILED` 指出是哪个 | 停下。**不要重试**：某个驱动停不掉，交接就不能做 |
| `refused_unknown_kernel` | 内核不是 `4.1.15-lab126` | 什么都没动 |

无论成功还是失败，脚本都会把停掉的东西放回去（报告末尾的 `[restore]` 段）：
先把根文件系统恢复可写，再重新加载驱动，最后让次核上线。
万一有 `RELOAD_FAILED=` 或 `CPU1_STILL_DOWN`，**重启一次设备就全好了**——没有写过任何分区。

## 4. 出意外时怎么办

| 现象 | 原因 | 怎么办 |
|---|---|---|
| `insmod` 之后卡住或重启 | 模块在原厂内核里出错 | 等。`panic_on_oops` 为 0 时可能只是进程终止；这台机器实测 `panic=5`，崩溃约 5 秒后自己重启回原厂 |
| 屏幕没反应但设备还活着 | 标记测试不碰显示；静止空跑停了 EPDC | 正常，等脚本结束 |
| 完全不响应 | — | 长按电源键约 40 秒强制重启。不要依赖 60 秒看门狗：内核还在跑时可能一直喂狗 |
| 重启后担心数据 | 没有写过任何分区 | 预期从原厂路径启动；恢复后看一眼书库和 KUAL |

## 5. 测完把报告带回来

两个目录拷回电脑就行，别只看屏幕上那一行：

```bash
cp -r /d/koa2-marker-test /d/koa2-quiesce-dryrun ~/koa2-evidence/
```

报告里只有内核身份、套件哈希、测试状态和耗时。
标记测试的报告只留含 `KOA2_HANDOFF` 的行，上一次启动的完整日志在设备上读完即删。
真要上传的话，先按[资料目录](../2026-11-21/README.md)那套做法去标识；
原始报告留本地，不进 Git。

## 6. 这个套件是什么、不是什么

| | |
|---|---|
| 是 | 已越狱 Oasis 2 / 固件 5.16.2.1.1 上的两个只读性质的测试 |
| 不是 | Android 安装包。这个分支目前没有可用的 KOA2 Android 包 |
| 不是 | 跳转。`execute_available` 恒为 0，跳转蹦床没编进来 |

套件里的 `zImage` 和 `initramfs.cpio.gz` 是标记测试要暂存的载荷（模块会校验它们的哈希），
**不会**被启动。

## 7. 这份套件的来历

| 文件 | SHA256 |
|---|---|
| `koa2_handoff_stage.ko` | `0699bef9d0a2…` |
| `koa2-stage-client` | `a39bbaf2cb35…` |
| `zImage` | `4921c4b2b1dc…` |
| `initramfs.cpio.gz` | `3aafcaf8924a…` |

完整校验值在 `readom-koa2-stage-test/SHA256SUMS`，设备脚本每次运行前自己核对一遍。

模块针对 `out-module-abi` 树编译，它导入的 35 个符号与原厂 CRC **全部一致**
（`crcs.json`，判据见[原厂模块 ABI](../../stock-module-abi.md)）；
构建脚本的三道门都过了：没有 GOT、没有写协处理器寄存器、没有导入 `cpu_down` 之类的符号。
`.ko` 的哈希和 11-21 那份不同，但大小一样、导入一样——构建在 `mktemp` 目录里进行，
目录名进了调试路径，不是代码变化。

主机测试 18 项通过、0 项失败（`python3 tools/run-tests.py --filter koa2`）。
其中 `koa2-marker-check` 是 2026-11-23 新加的，专门守着上面第 2 节说的那个误报。

上一次实机测试（2026-11-21，暂存检查 `sealed_plan_valid`）的报告和证据在
[reports/2026-11-21-stage-test](../../reports/2026-11-21-stage-test/README.md)。
今晚这两项不重跑暂存测试：模块虽然重建过，CRC 和导入都没变，而标记测试本身就会
`insmod` 一次，加载不了会直接报 `insmod_refused`。

测完之后的下一步见[当前状态](../../STATUS.md)和[换电脑继续](../../CONTINUE.md)；
完整设备流程在[实机操作手册](../../device-runbook.md)（本文件是它第 8b、8c 两节的操作单）。
