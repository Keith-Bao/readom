# 设备测试流程（Oasis 2）

这份文件是给**接手执行的人或 agent** 看的，不需要读过之前的对话。按顺序做，
每一步都写清楚了要什么、会得到什么、出问题怎么办。英文版见
[device-runbook.en.md](device-runbook.en.md)。

**这次要做的事**：把暂存模块装进原厂内核，让它在设备上检查交接的全部准备工作，
然后卸载。**不会跳转到新内核**，模块里没有任何代码能做到这件事。

**不会发生的事**：不写分区、不改引导、不改 IDME、不动校准数据、不联网、不重启。
所有写入都在 Kindle 的 `koa2-stage-test/` 目录里。

## 0. 先确认这些前提

| 前提 | 怎么确认 | 不满足怎么办 |
|---|---|---|
| Kindle 是 Oasis 2，固件 5.16.2.1.1 | `/etc/prettyversion.txt` | 停下。本套件只认这一个固件 |
| 内核是 `4.1.15-lab126` | `uname -a` | 停下。脚本自己也会拒绝 |
| Kindle 已越狱并装了 KUAL | 能看到 KUAL 菜单 | 停下，先处理越狱 |
| 电量 > 50% | 设备状态栏 | 先充电。测试中途断电不会损坏，但会白跑一次 |
| 构建主机是 WSL `Readom-KOA2` | `wsl.exe -l -v` | 见 [build-environment.md](build-environment.md) |

## 1. 在主机上造套件

在 WSL 里（不要在 Windows 侧操作仓库文件，公司 DLP 会加密 `.py`/`.json`）：

```bash
cd ~/readom-work/readom && bash ports/koa2/build-stage-test-kit.sh
```

产物在 `~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test/`，共 7 个文件：
`stage-test.sh`、`koa2_handoff_stage.ko`、`koa2-stage-client`、`zImage`、
`initramfs.cpio.gz`、`SHA256SUMS`、`menu.json`、`config.xml`。

脚本在导出前自己会拒绝这些情况：模块需要 GOT、导入了构建树没导出的符号、
写协处理器寄存器、导入 `cpu_down`/重启类符号、zImage 与模块钉的哈希不一致。
**它拒绝就不要往设备上拷**，先解决。

## 2. 拷到 Kindle

USB 连上，在 WSL 里以 root 挂载并复制：

```bash
sudo mount -t drvfs F: /mnt/f && cp -r ~/artifacts/koa2-stage-test-kit/readom-koa2-stage-test /mnt/f/extensions/
```

复制完必须验这一句，确认 DLP 没有加密 JSON：

```bash
head -c 8 /mnt/f/extensions/readom-koa2-stage-test/menu.json
```

必须输出 `{"items"`。如果是 `%TSD-Head`，说明文件被加密了，KUAL 读不出菜单，
换一种复制方式（别经 Windows 资源管理器）。`.ko`、zImage 和脚本被改动时设备上的
哈希检查会拒绝运行，但 `menu.json` 和 `config.xml` 不在哈希表里，只能这样手验。

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
| `insmod_refused` | **最可能**。模块是对 headless-smoke 树编的，符号 CRC 与原厂对不上，内核按规则拒绝，什么都没加载 | 走第 6 节。原厂 `.ko` 已经拷回来了，正好用来核对 |
| `sealed_plan_valid` | 模块被接受，设备上的全部检查通过并已卸载 | 走第 7 节 |
| `seal_failed` | 模块装上了但封存被拒 | 看 `seal_stage`，对照第 5 节 |
| `refused_unknown_kernel` | 内核不是 `4.1.15-lab126` | 确认设备和固件，什么都没加载 |
| `refused_kit_damaged` | 套件哈希不符 | 重新执行第 1、2 节 |
| `refused_module_already_loaded` | 模块已经在内核里 | 先 `rmmod koa2_handoff_stage` |

`client_exit=0` 那一行后面还有一行数据，含义见下：

| 字段 | 应该是 |
|---|---|
| `state` | 2（已封存） |
| `plan_valid` | 1 |
| `execute_available` | **恒为 0**。这个阶段没有跳转，是 1 就说明装错了东西 |
| `fdt_bytes` | 约 47900（原厂 FDT 改写后，比原件小） |
| `payload_pages` | 约 1690（三个载荷占的页数） |
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

## 6. 如果是 insmod_refused（最可能）

把报告拷回主机并核对 CRC：

```bash
mkdir -p ~/koa2-evidence/stage-test && sudo mount -t drvfs F: /mnt/f && cp -r /mnt/f/koa2-stage-test/* ~/koa2-evidence/stage-test/
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

主机侧和设备侧能验的都验完了。把报告拷回主机（命令同第 6 节），
更新 [STATUS.md](STATUS.md)，然后**停在这里**。

下一个阶段是真正的交接执行，它还缺两样东西，都不在这份流程里：
静止顺序里真正碰硬件的 run/undo（[quiesce.c](../../boot/koa2/quiesce.c) 目前只有可测的顺序），
以及在交接当次读出 EPDC 帧缓冲地址加入排除区。
跳转蹦床（[jump.S](../../boot/koa2/jump.S)）已写好并在 QEMU 的 Cortex-A7 上裸机跑过，
但没有编进任何模块，也从未在设备上执行。

## 8. 出意外时怎么办

| 现象 | 原因 | 怎么办 |
|---|---|---|
| 设备在 `insmod` 之后卡住或重启 | 模块在原厂内核里出错（oops） | 等。`panic_on_oops` 为 1 时原厂内核约 5 秒后自己重启回原厂；为 0 时只有那个进程死掉 |
| 屏幕没反应但设备还活着 | 测试不碰显示，正常 | 等脚本结束，看报告 |
| 完全不响应 | — | 长按电源键约 40 秒强制重启。看门狗 60 秒也会复位 |
| 重启后担心数据 | 没有写过任何分区 | 原厂系统照常启动，书库和 KUAL 不受影响 |

重启走原厂路径时，原厂内核会照常把 printk 写到 eMMC 扇区 0x9a400。
这是它对任何崩溃都会做的事，与交接无关，见
[boot-preflight-findings.md](boot-preflight-findings.md) 末节。

## 9. 还需要设备、但不在这次流程里的事

| 事 | 为什么必须上机 |
|---|---|
| 标准 sdhci 驱动在这块 eMMC 上的速度模式与 tuning | 只能上机测 |
| EPDC 帧缓冲每次启动的实际地址 | 动态分配，要当次读 |
| 静止顺序每一步对硬件的作用 | 现在只验了顺序和回滚 |
| printk 缓冲能否活过 WDOG_B 复位 | 只能上机测 |
| 交接执行（复制并跳转） | 以上都确认之后才谈 |
